#include "frogjs/repl.h"
#include "frogjs/errors.h"
#include "frogjs/line_editor.h"
#include "frogjs/util.h"
#include "frogjs/version.h"
#include <v8.h>
#include <uv.h>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace v8;


namespace {

const char* kHelpText =
    ".break    Sometimes you get stuck, this gets you out\n"
    ".clear    Break, and also clear the local context\n"
    ".exit     Exit the REPL\n"
    ".help     Print this help message\n"
    ".load     Load JS from a file into the REPL session\n"
    ".save     Save all evaluated commands in this REPL session to a file\n"
    "\n"
    "Press Ctrl+C to abort current expression, Ctrl+D to exit the REPL\n";

const char* kCommands[] = {".break", ".clear", ".exit", ".help", ".load", ".save"};

const char* kKeywords[] = {
    "async", "await", "break", "case", "catch", "class", "const", "continue", "debugger",
    "default", "delete", "do", "else", "export", "extends", "false", "finally", "for",
    "function", "if", "import", "in", "instanceof", "let", "new", "null", "return",
    "static", "super", "switch", "this", "throw", "true", "try", "typeof", "undefined",
    "var", "void", "while", "with", "yield",
};

// Lists every identifier-like property name of a value and its prototypes
const char* kCompleterSource = R"JS(
(function (target) {
  const names = new Set();
  for (let obj = Object(target); obj !== null; obj = Object.getPrototypeOf(obj)) {
    for (const key of Object.getOwnPropertyNames(obj)) names.add(key);
  }
  return [...names].filter((key) => /^[A-Za-z_$][\w$]*$/.test(key));
})
)JS";

struct ReplState {
    Isolate* isolate = nullptr;
    Global<Context> context;
    frogjs::ContextFactory new_context = nullptr;
    bool terminal = false;      // stdin and stdout are a TTY: line editing, history, completion
    bool colors = false;        // colorize results
    bool running = false;
    bool prompt_shown = false;  // the line editor owns the current terminal line
    std::unique_ptr<frogjs::LineEditor> editor;
    std::string buffer;         // multi-line input waiting to be completed
    std::vector<std::string> session;  // lines entered, for .save
    std::set<std::string> declared;    // top-level let/const/class names, for completion
    std::string history_path;
    std::string pending_input;  // non-terminal mode: bytes not yet split into lines
    bool exit_warned = false;   // Ctrl+C was pressed once on an empty line
    int counter = 0;            // names scripts REPL1, REPL2, ...

    uv_poll_t stdin_poll;
    bool poll_active = false;
    uv_async_t sigint_async;
    int sigint_pipe[2] = {-1, -1};
    std::thread watchdog;
    std::mutex eval_mutex;
    bool evaluating = false;
    struct sigaction old_sigint;
};

ReplState* g_repl = nullptr;
volatile sig_atomic_t g_sigint_fd = -1;

const char* Prompt() {
    return g_repl->buffer.empty() ? "> " : "... ";
}

std::string Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string ToStdString(Isolate* isolate, Local<Value> value) {
    String::Utf8Value str(isolate, value);
    return *str != nullptr ? std::string(*str, str.length()) : std::string();
}

bool IsIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool IsIdentifierPart(char c) {
    return IsIdentifierStart(c) || std::isdigit(static_cast<unsigned char>(c));
}

// "a", "a.b.c" - expressions that are safe to evaluate for tab completion
bool IsIdentifierChain(const std::string& expr) {
    bool expect_start = true;
    for (char c : expr) {
        if (expect_start) {
            if (!IsIdentifierStart(c)) return false;
            expect_start = false;
        } else if (c == '.') {
            expect_start = true;
        } else if (!IsIdentifierPart(c)) {
            return false;
        }
    }
    return !expect_start;
}

// Remember names declared with let/const/class: they live in the script scope,
// not on the global object, so completion can't find them by reflection
void RecordDeclarations(const std::string& code) {
    static const char* kDeclarators[] = {"let", "const", "class"};
    for (const char* declarator : kDeclarators) {
        size_t len = strlen(declarator);
        size_t pos = 0;
        while ((pos = code.find(declarator, pos)) != std::string::npos) {
            bool word_start = pos == 0 || !IsIdentifierPart(code[pos - 1]);
            pos += len;
            if (!word_start || pos >= code.size() || !std::isspace(static_cast<unsigned char>(code[pos]))) continue;
            while (pos < code.size() && std::isspace(static_cast<unsigned char>(code[pos]))) pos++;
            size_t name_end = pos;
            while (name_end < code.size() && IsIdentifierPart(code[name_end])) name_end++;
            if (name_end > pos && IsIdentifierStart(code[pos])) {
                g_repl->declared.insert(code.substr(pos, name_end - pos));
            }
        }
    }
}

std::string DefaultHistoryPath() {
    const char* env = getenv("FROGJS_REPL_HISTORY");
    if (env != nullptr) return env;  // empty disables history
    const char* home = getenv("HOME");
    return home != nullptr ? std::string(home) + "/.frogjs_repl_history" : "";
}

void SaveHistory() {
    if (g_repl->editor && !g_repl->history_path.empty()) g_repl->editor->SaveHistory(g_repl->history_path);
}

// ---------------------------------------------------------------------------
// Output

void PrintResult(const std::string& text) {
    fwrite(text.data(), 1, text.size(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

// Frames of the REPL's own top-level code ("at REPL3:1:7") add nothing
std::string StripReplFrames(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    std::string out;
    while (std::getline(in, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.rfind("at REPL", 0) == 0 && trimmed.find(' ', 3) == std::string::npos) continue;
        if (!out.empty()) out += '\n';
        out += line;
    }
    return out;
}

void PrintException(Local<Context> context, TryCatch& try_catch) {
    Isolate* isolate = g_repl->isolate;
    Local<Value> exception = try_catch.Exception();
    context->Global()->Set(context, String::NewFromUtf8Literal(isolate, "_error"), exception).Check();
    PrintResult("Uncaught " + StripReplFrames(frogjs::Inspect(isolate, context, exception, g_repl->colors)));
}

// ---------------------------------------------------------------------------
// Evaluation

// Incomplete input ("function f() {", "[1,") continues on the next line
bool IsRecoverable(TryCatch& try_catch, const std::string& code) {
    std::string error = ToStdString(g_repl->isolate, try_catch.Exception());
    if (error.rfind("SyntaxError:", 0) != 0) return false;
    if (error.find("Unterminated template literal") != std::string::npos) return true;

    // Only errors at the very end of the input mean "keep typing"
    bool at_end = true;
    Local<Message> message = try_catch.Message();
    if (!message.IsEmpty()) {
        size_t content_end = code.find_last_not_of(" \t\r\n");
        content_end = content_end == std::string::npos ? 0 : content_end + 1;
        at_end = static_cast<size_t>(message->GetStartPosition()) >= content_end;
    }
    return at_end && (error.find("Unexpected end of input") != std::string::npos ||
                      error.find("missing ) after argument list") != std::string::npos);
}

MaybeLocal<Script> CompileInput(Local<Context> context, const std::string& code, const std::string& name) {
    Isolate* isolate = g_repl->isolate;
    ScriptOrigin origin(String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked());
    Local<String> source;
    if (!String::NewFromUtf8(isolate, code.c_str(), NewStringType::kNormal,
            static_cast<int>(code.size())).ToLocal(&source)) {
        return MaybeLocal<Script>();
    }
    return Script::Compile(context, source, &origin);
}

// Evaluate the buffered input; keeps it buffered if it is incomplete
void Evaluate() {
    Isolate* isolate = g_repl->isolate;
    HandleScope handle_scope(isolate);
    Local<Context> context = g_repl->context.Get(isolate);
    Context::Scope context_scope(context);

    std::string code = g_repl->buffer;
    std::string trimmed = Trim(code);
    if (trimmed.empty()) {
        g_repl->buffer.clear();
        return;
    }

    std::string name = "REPL" + std::to_string(++g_repl->counter);
    TryCatch try_catch(isolate);
    Local<Script> script;

    // "{ a: 1 }" is an object literal at the prompt, not a block
    bool compiled = false;
    if (trimmed.front() == '{' && trimmed.back() != ';') {
        compiled = CompileInput(context, "(" + code + ")", name).ToLocal(&script);
        if (!compiled) try_catch.Reset();
    }
    if (!compiled && !CompileInput(context, code, name).ToLocal(&script)) {
        if (IsRecoverable(try_catch, code)) {
            g_repl->counter--;
            return;
        }
        g_repl->buffer.clear();
        PrintException(context, try_catch);
        return;
    }
    g_repl->buffer.clear();
    RecordDeclarations(code);

    // While the script runs, Ctrl+C terminates it (see Watchdog)
    {
        std::lock_guard<std::mutex> lock(g_repl->eval_mutex);
        g_repl->evaluating = true;
    }
    Local<Value> result;
    bool ok = script->Run(context).ToLocal(&result);
    {
        std::lock_guard<std::mutex> lock(g_repl->eval_mutex);
        g_repl->evaluating = false;
    }

    if (try_catch.HasTerminated()) {
        isolate->CancelTerminateExecution();
        PrintResult("Script execution was interrupted by `SIGINT`");
        return;
    }
    // A Ctrl+C that arrived just as the script finished must not hit the next one
    isolate->CancelTerminateExecution();

    if (!ok) {
        PrintException(context, try_catch);
        return;
    }
    context->Global()->Set(context, String::NewFromUtf8Literal(isolate, "_"), result).Check();
    PrintResult(frogjs::Inspect(isolate, context, result, g_repl->colors));
}

void ResetContext() {
    Isolate* isolate = g_repl->isolate;
    HandleScope handle_scope(isolate);
    g_repl->context.Reset(isolate, g_repl->new_context(isolate));
    g_repl->declared.clear();
    // StartREPL's loop enters the new context before running the event loop again
    uv_stop(uv_default_loop());
}

// ---------------------------------------------------------------------------
// Commands

void Close();

bool IsCommand(const std::string& name) {
    for (const char* command : kCommands) {
        if (name == command) return true;
    }
    return false;
}

void RunCommand(const std::string& line) {
    size_t space = line.find_first_of(" \t");
    std::string command = line.substr(0, space);
    std::string arg = space == std::string::npos ? "" : Trim(line.substr(space));

    if (command == ".exit") {
        Close();
    } else if (command == ".help") {
        fputs(kHelpText, stdout);
    } else if (command == ".break") {
        g_repl->buffer.clear();
    } else if (command == ".clear") {
        g_repl->buffer.clear();
        PrintResult("Clearing context...");
        ResetContext();
    } else if (command == ".load") {
        std::ifstream file(arg);
        if (arg.empty() || !file.is_open()) {
            PrintResult("Failed to load: " + arg);
            return;
        }
        std::stringstream content;
        content << file.rdbuf();
        g_repl->buffer = content.str() + "\n";
        g_repl->session.push_back(content.str());
        Evaluate();
        g_repl->buffer.clear();
    } else if (command == ".save") {
        std::ofstream file(arg);
        if (arg.empty() || !file.is_open()) {
            PrintResult("Failed to save: " + arg);
            return;
        }
        for (const std::string& entry : g_repl->session) file << entry << "\n";
        PrintResult("Session saved to: " + arg);
    } else {
        PrintResult("Invalid REPL keyword");
    }
    fflush(stdout);
}

void HandleLine(const std::string& input) {
    g_repl->exit_warned = false;

    // ".help" is a command, ".5" is a number. Commands also work inside
    // multi-line input (.break); other ".name" lines there continue a
    // method chain.
    std::string trimmed = Trim(input);
    if (trimmed.size() > 1 && trimmed[0] == '.' && !std::isdigit(static_cast<unsigned char>(trimmed[1]))) {
        std::string name = trimmed.substr(0, trimmed.find_first_of(" \t"));
        if (IsCommand(name) || g_repl->buffer.empty()) {
            RunCommand(trimmed);
            return;
        }
    }

    g_repl->session.push_back(input);
    g_repl->buffer += input + "\n";
    Evaluate();
}

// ---------------------------------------------------------------------------
// Tab completion

std::vector<std::string> PropertyNames(Local<Context> context, const std::string& expr) {
    Isolate* isolate = g_repl->isolate;
    std::vector<std::string> names;
    TryCatch try_catch(isolate);

    Local<Value> target = context->Global();
    if (!expr.empty()) {
        Local<Script> script;
        if (!CompileInput(context, "(" + expr + ")", "[completion]").ToLocal(&script) ||
            !script->Run(context).ToLocal(&target) || target->IsNullOrUndefined()) {
            return names;
        }
    }

    Local<Script> script;
    Local<Value> lister;
    Local<Value> result;
    if (!CompileInput(context, kCompleterSource, "[completion]").ToLocal(&script) ||
        !script->Run(context).ToLocal(&lister) || !lister->IsFunction() ||
        !lister.As<Function>()->Call(context, Undefined(isolate), 1, &target).ToLocal(&result) ||
        !result->IsArray()) {
        return names;
    }

    Local<Array> array = result.As<Array>();
    for (uint32_t i = 0; i < array->Length(); i++) {
        Local<Value> item;
        if (array->Get(context, i).ToLocal(&item)) names.push_back(ToStdString(isolate, item));
    }
    return names;
}

// Candidates for the word before the cursor: a REPL command, a global name, or
// a property of an identifier chain ("process.ve" -> "process.version", ...)
std::vector<std::string> CompleteLine(const std::string& line, size_t* word_start) {
    size_t start = line.size();
    while (start > 0 && (IsIdentifierPart(line[start - 1]) || line[start - 1] == '.')) start--;
    *word_start = start;
    std::string word = line.substr(start);
    std::vector<std::string> matches;

    if (g_repl->buffer.empty() && Trim(line.substr(0, start)).empty() && !word.empty() && word[0] == '.') {
        for (const char* command : kCommands) {
            if (std::string(command).rfind(word, 0) == 0) matches.push_back(command);
        }
        return matches;
    }

    size_t dot = word.rfind('.');
    std::string expr = dot == std::string::npos ? "" : word.substr(0, dot);
    std::string prefix = dot == std::string::npos ? word : word.substr(dot + 1);
    if (!expr.empty() && !IsIdentifierChain(expr)) return matches;

    Isolate* isolate = g_repl->isolate;
    HandleScope handle_scope(isolate);
    Local<Context> context = g_repl->context.Get(isolate);
    Context::Scope context_scope(context);

    std::set<std::string> names;
    for (const std::string& name : PropertyNames(context, expr)) names.insert(name);
    if (expr.empty()) {
        for (const char* keyword : kKeywords) names.insert(keyword);
        names.insert(g_repl->declared.begin(), g_repl->declared.end());
    }

    for (const std::string& name : names) {
        if (name.rfind(prefix, 0) == 0) matches.push_back(expr.empty() ? name : expr + "." + name);
    }
    return matches;
}

// ---------------------------------------------------------------------------
// Input

void ShowPrompt() {
    if (!g_repl->running) return;
    if (g_repl->terminal) {
        g_repl->editor->Start(Prompt());
        g_repl->prompt_shown = true;
    } else {
        fputs(Prompt(), stdout);
        fflush(stdout);
    }
}

// Back to the normal terminal mode while input runs (Ctrl+C becomes SIGINT)
void HidePrompt() {
    if (g_repl->terminal && g_repl->prompt_shown) {
        g_repl->editor->Stop();
        g_repl->prompt_shown = false;
    }
}

// Ctrl+C clears the current input; on an empty line, twice in a row exits
void OnInterrupt(const std::string& line) {
    bool had_input = !line.empty() || !g_repl->buffer.empty();
    HidePrompt();
    g_repl->buffer.clear();
    if (!had_input) {
        if (g_repl->exit_warned) {
            Close();
            return;
        }
        g_repl->exit_warned = true;
        fputs("(To exit, press Ctrl+C again or Ctrl+D or type .exit)\n", stdout);
        fflush(stdout);
    }
    ShowPrompt();
}

void FeedTerminal(unsigned char byte) {
    std::string line;
    switch (g_repl->editor->Feed(byte, &line)) {
        case frogjs::LineEditor::Event::kLine:
            HidePrompt();
            g_repl->editor->AddHistory(line);
            HandleLine(line);
            ShowPrompt();
            break;
        case frogjs::LineEditor::Event::kEof:
            HidePrompt();
            Close();
            break;
        case frogjs::LineEditor::Event::kInterrupt:
            OnInterrupt(line);
            break;
        case frogjs::LineEditor::Event::kNone:
            break;
    }
}

// Non-terminal input (a pipe or file): no line editing, just lines
void ProcessPendingLines(bool at_eof) {
    size_t newline;
    while (g_repl->running && (newline = g_repl->pending_input.find('\n')) != std::string::npos) {
        std::string line = g_repl->pending_input.substr(0, newline);
        g_repl->pending_input.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        HandleLine(line);
        ShowPrompt();
    }
    if (at_eof && g_repl->running) {
        if (!g_repl->pending_input.empty()) {
            std::string line;
            line.swap(g_repl->pending_input);
            HandleLine(line);
        }
        if (g_repl->running) fputc('\n', stdout);
        Close();
    }
}

void ReadStdin() {
    char chunk[4096];
    ssize_t n = read(STDIN_FILENO, chunk, sizeof(chunk));
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;

    if (g_repl->terminal) {
        if (n <= 0) {
            HidePrompt();
            Close();
            return;
        }
        for (ssize_t i = 0; i < n && g_repl->running; i++) FeedTerminal(static_cast<unsigned char>(chunk[i]));
    } else if (n > 0) {
        g_repl->pending_input.append(chunk, static_cast<size_t>(n));
        ProcessPendingLines(false);
    } else {
        ProcessPendingLines(true);
    }
}

void OnStdinReadable(uv_poll_t* handle, int status, int events) {
    (void)handle;
    (void)events;
    if (status < 0) {
        Close();
        return;
    }
    ReadStdin();
}

// ---------------------------------------------------------------------------
// Ctrl+C while code runs
//
// At the prompt the terminal is in raw mode and Ctrl+C arrives as a byte.
// While code runs it is a SIGINT: the handler only writes to a pipe, and a
// watchdog thread reading it terminates the script (TerminateExecution may be
// called from another thread). A SIGINT at the prompt (kill -INT) is passed to
// the event loop and treated like Ctrl+C.

void OnSigint(int) {
    if (g_sigint_fd >= 0) {
        char byte = 'i';
        ssize_t ignored = write(g_sigint_fd, &byte, 1);
        (void)ignored;
    }
}

void Watchdog(ReplState* repl) {
    char byte;
    while (read(repl->sigint_pipe[0], &byte, 1) == 1 && byte == 'i') {
        std::lock_guard<std::mutex> lock(repl->eval_mutex);
        if (repl->evaluating) {
            repl->isolate->TerminateExecution();
        } else {
            uv_async_send(&repl->sigint_async);
        }
    }
}

void OnSigintAtPrompt(uv_async_t* handle) {
    (void)handle;
    if (g_repl->running && g_repl->prompt_shown) FeedTerminal(3);
}

void InstallSigint() {
    if (pipe(g_repl->sigint_pipe) != 0) return;
    g_sigint_fd = g_repl->sigint_pipe[1];
    uv_async_init(uv_default_loop(), &g_repl->sigint_async, OnSigintAtPrompt);
    g_repl->watchdog = std::thread(Watchdog, g_repl);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = OnSigint;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &action, &g_repl->old_sigint);
}

void UninstallSigint() {
    if (g_repl->sigint_pipe[1] < 0) return;
    sigaction(SIGINT, &g_repl->old_sigint, nullptr);
    g_sigint_fd = -1;
    char quit = 'q';
    ssize_t ignored = write(g_repl->sigint_pipe[1], &quit, 1);
    (void)ignored;
    if (g_repl->watchdog.joinable()) g_repl->watchdog.join();
    close(g_repl->sigint_pipe[0]);
    close(g_repl->sigint_pipe[1]);
    g_repl->sigint_pipe[0] = g_repl->sigint_pipe[1] = -1;
    uv_close(reinterpret_cast<uv_handle_t*>(&g_repl->sigint_async), nullptr);
}

// ---------------------------------------------------------------------------
// Lifetime

void Close() {
    if (!g_repl->running) return;
    g_repl->running = false;
    HidePrompt();
    SaveHistory();
    if (g_repl->poll_active) {
        uv_poll_stop(&g_repl->stdin_poll);
        uv_close(reinterpret_cast<uv_handle_t*>(&g_repl->stdin_poll), nullptr);
        g_repl->poll_active = false;
    }
    UninstallSigint();
    fflush(stdout);
    // Like Node.js, leaving the REPL ends the process even if timers are pending
    uv_stop(uv_default_loop());
}

// process.exit() from the prompt must still restore the terminal
void RestoreTerminalAtExit() {
    if (g_repl != nullptr && g_repl->running) {
        if (g_repl->editor) g_repl->editor->Stop();
        SaveHistory();
    }
}

}  // namespace

namespace frogjs {

void ReplBeforeOutput() {
    if (g_repl != nullptr && g_repl->prompt_shown) g_repl->editor->Hide();
}

void ReplAfterOutput() {
    if (g_repl != nullptr && g_repl->prompt_shown) g_repl->editor->Refresh();
}

int StartREPL(Isolate* isolate, Local<Context> context, ContextFactory new_context) {
    ReplState state;
    g_repl = &state;
    state.isolate = isolate;
    state.context.Reset(isolate, context);
    state.new_context = new_context;
    state.terminal = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    state.colors = state.terminal && getenv("NO_COLOR") == nullptr;
    state.running = true;
    SetReplActive(true);

    uv_loop_t* loop = uv_default_loop();
    if (uv_poll_init(loop, &state.stdin_poll, STDIN_FILENO) == 0) {
        uv_poll_start(&state.stdin_poll, UV_READABLE, OnStdinReadable);
        state.poll_active = true;
    }

    if (state.terminal) {
        static bool registered = false;
        if (!registered) {
            atexit(RestoreTerminalAtExit);
            registered = true;
        }
        state.editor = std::make_unique<LineEditor>(STDIN_FILENO);
        state.editor->SetCompleter(CompleteLine);
        state.history_path = DefaultHistoryPath();
        if (!state.history_path.empty()) state.editor->LoadHistory(state.history_path);
        InstallSigint();
        printf("Welcome to FrogJS v%s.\nType \".help\" for more information.\n", FROGJS_VERSION);
        fflush(stdout);
    }

    ShowPrompt();

    if (!state.poll_active) {
        // stdin can't be polled (e.g. a regular file): read it synchronously
        while (state.running) ReadStdin();
    }

    // Run the event loop until the REPL closes. .clear stops the loop so it
    // can be re-entered with the new context.
    while (state.running) {
        HandleScope handle_scope(isolate);
        Local<Context> current = state.context.Get(isolate);
        Context::Scope context_scope(current);
        uv_run(loop, UV_RUN_ONCE);
        DrainTasks(isolate);
    }

    SetReplActive(false);
    state.editor.reset();
    state.context.Reset();
    g_repl = nullptr;
    return 0;
}

}  // namespace frogjs
