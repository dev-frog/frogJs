#include "frogjs/repl.h"
#include "frogjs/errors.h"
#include "frogjs/line_editor.h"
#include "frogjs/util.h"
#include "frogjs/v8_compat.h"
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
    ".editor   Enter editor mode\n"
    ".exit     Exit the REPL\n"
    ".help     Print this help message\n"
    ".load     Load JS from a file into the REPL session\n"
    ".save     Save all evaluated commands in this REPL session to a file\n"
    "\n"
    "Press Ctrl+C to abort current expression, Ctrl+D to exit the REPL\n";

const char* kCommands[] = {".break", ".clear", ".editor", ".exit", ".help", ".load", ".save"};

const char* kKeywords[] = {
    "async", "await", "break", "case", "catch", "class", "const", "continue", "debugger",
    "default", "delete", "do", "else", "export", "extends", "false", "finally", "for",
    "function", "if", "import", "in", "instanceof", "let", "new", "null", "return",
    "static", "super", "switch", "this", "throw", "true", "try", "typeof", "undefined",
    "var", "void", "while", "with", "yield",
};

// Top-level await at the prompt: input that only compiles with await becomes
// an async function. Its declarations become globals (var), the last
// expression is returned as { value }, and the REPL shows it when it settles.
// It tokenizes just enough JavaScript to find statements and bindings.
const char* kTopLevelAwaitSource = R"JS(
(function (src) {
'use strict';
  // ---- Tokens: just enough of JavaScript to find statements and bindings ----
  const puncts = ['>>>=', '...', '===', '!==', '**=', '<<=', '>>=', '>>>', '&&=', '||=', '??=', '=>', '==', '!=', '<=', '>=',
    '&&', '||', '??', '?.', '++', '--', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '**', '<<', '>>'];
  const regexAfterName = /^(?:return|typeof|instanceof|in|of|new|delete|void|throw|case|do|else|yield|await)$/;
  const tokens = [];
  let i = 0;
  let newline = false;
  const fail = () => { throw new SyntaxError('cannot rewrite'); };

  function skipTemplate() {
    // i is after the opening backtick
    while (i < src.length) {
      const c = src[i];
      if (c === '\\') { i += 2; continue; }
      if (c === '`') { i++; return; }
      if (c === '$' && src[i + 1] === '{') {
        i += 2;
        let depth = 1;
        while (i < src.length && depth > 0) {
          const d = src[i];
          if (d === '{') depth++;
          else if (d === '}') depth--;
          else if (d === '"' || d === "'") { skipString(d); continue; }
          else if (d === '`') { i++; skipTemplate(); continue; }
          i++;
        }
        continue;
      }
      i++;
    }
    fail();
  }
  function skipString(quote) {
    i++;
    while (i < src.length && src[i] !== quote) {
      if (src[i] === '\\') i++;
      else if (src[i] === '\n') fail();
      i++;
    }
    if (i >= src.length) fail();
    i++;
  }
  function regexAllowed() {
    const prev = tokens[tokens.length - 1];
    if (!prev) return true;
    if (prev.type === 'punct') return !/^[)\]}]$/.test(prev.value) && prev.value !== '++' && prev.value !== '--';
    if (prev.type === 'name') return regexAfterName.test(prev.value);
    return false;
  }

  while (i < src.length) {
    const c = src[i];
    if (c === '\n') { newline = true; i++; continue; }
    if (/\s/.test(c)) { i++; continue; }
    if (src.startsWith('//', i)) { const end = src.indexOf('\n', i); i = end < 0 ? src.length : end; continue; }
    if (src.startsWith('/*', i)) {
      const end = src.indexOf('*/', i + 2);
      if (end < 0) fail();
      if (src.slice(i, end).includes('\n')) newline = true;
      i = end + 2;
      continue;
    }
    const start = i;
    let type;
    if (c === '"' || c === "'") { skipString(c); type = 'string'; }
    else if (c === '`') { i++; skipTemplate(); type = 'template'; }
    else if (/[A-Za-z_$\u0080-￿]/.test(c)) { while (i < src.length && /[\w$\u0080-￿]/.test(src[i])) i++; type = 'name'; }
    else if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(src[i + 1]))) { while (i < src.length && /[\w.]/.test(src[i])) i++; type = 'number'; }
    else if (c === '/' && regexAllowed()) {
      i++;
      let inClass = false;
      while (i < src.length && (src[i] !== '/' || inClass)) {
        if (src[i] === '\\') i++;
        else if (src[i] === '[') inClass = true;
        else if (src[i] === ']') inClass = false;
        else if (src[i] === '\n') fail();
        i++;
      }
      i++;
      while (i < src.length && /\w/.test(src[i])) i++;
      type = 'regex';
    } else {
      const p = puncts.find((candidate) => src.startsWith(candidate, i));
      i += p ? p.length : 1;
      type = 'punct';
    }
    tokens.push({ type, value: src.slice(start, i), start, end: i, newline });
    newline = false;
  }

  const is = (t, value) => t && t.type !== 'string' && t.type !== 'template' && t.value === value;

  // ---- Statements: [first token, last token] at the top level ----
  const blockStatements = new Set(['if', 'for', 'while', 'try', 'switch', 'function', 'class', 'with']);
  const continuesAfterBlock = new Set(['else', 'catch', 'finally']);
  // A newline ends a statement unless one side can't end or start one
  function continues(prev, next) {
    if (prev.type === 'punct' && !/^(?:\)|\]|\}|\+\+|--)$/.test(prev.value)) return true;
    if (prev.type === 'name' && /^(?:typeof|instanceof|in|of|new|delete|void|await|yield|extends|async)$/.test(prev.value)) return true;
    if (next.type === 'punct' && !/^(?:\{|!|~|\+\+|--|\+|-)$/.test(next.value)) return true;
    if (next.type === 'name' && /^(?:in|of|instanceof)$/.test(next.value)) return true;
    if (next.type === 'template') return true;
    return false;
  }
  const statements = [];
  let first = 0;
  let depth = 0;
  for (let k = 0; k < tokens.length; k++) {
    const t = tokens[k];
    if (k > first && depth === 0 && t.newline && !continues(tokens[k - 1], t)) {
      statements.push([first, k - 1]);
      first = k;
    }
    if (t.type === 'punct' && (t.value === '(' || t.value === '[' || t.value === '{' || t.value === '${')) depth++;
    else if (t.type === 'punct' && (t.value === ')' || t.value === ']' || t.value === '}')) {
      depth--;
      if (depth < 0) fail();
      const head = tokens[first];
      const block = blockStatements.has(head.value) || (is(head, 'async') && is(tokens[first + 1], 'function')) || is(head, '{');
      if (depth === 0 && t.value === '}' && block && !continuesAfterBlock.has((tokens[k + 1] || {}).value) &&
          !(is(head, 'do'))) {
        statements.push([first, k]);
        first = k + 1;
      }
    } else if (depth === 0 && is(t, ';')) {
      statements.push([first, k]);
      first = k + 1;
    }
  }
  if (first < tokens.length) statements.push([first, tokens.length - 1]);

  // ---- Binding names in a declaration pattern ----
  function patternNames(from, to, names) {
    let k = from;
    function skipDefault() {
      if (!is(tokens[k], '=')) return;
      let level = 0;
      for (k++; k <= to; k++) {
        const v = tokens[k].type === 'punct' ? tokens[k].value : '';
        if (v === '(' || v === '[' || v === '{') level++;
        else if (v === ')' || v === ']' || v === '}') { if (level === 0) return; level--; }
        else if (v === ',' && level === 0) return;
      }
    }
    function binding() {
      const t = tokens[k];
      if (!t) fail();
      if (t.type === 'name') { names.push(t.value); k++; }
      else if (is(t, '{')) objectPattern();
      else if (is(t, '[')) arrayPattern();
      else fail();
      skipDefault();
    }
    function objectPattern() {
      k++;
      while (k <= to && !is(tokens[k], '}')) {
        if (is(tokens[k], '...')) { k++; binding(); }
        else {
          let shorthand = null;
          if (is(tokens[k], '[')) {
            let level = 0;
            for (; k <= to; k++) {
              if (is(tokens[k], '[')) level++;
              else if (is(tokens[k], ']') && --level === 0) break;
            }
            k++;
          } else {
            shorthand = tokens[k].type === 'name' ? tokens[k].value : null;
            k++;
          }
          if (is(tokens[k], ':')) { k++; binding(); }
          else if (shorthand) { names.push(shorthand); skipDefault(); }
          else fail();
        }
        if (is(tokens[k], ',')) k++;
      }
      k++;
    }
    function arrayPattern() {
      k++;
      while (k <= to && !is(tokens[k], ']')) {
        if (is(tokens[k], ',')) { k++; continue; }
        if (is(tokens[k], '...')) k++;
        binding();
        if (is(tokens[k], ',')) k++;
      }
      k++;
    }
    binding();
    return k;
  }

  // ---- The rewrite ----
  const hoisted = [];
  const body = [];
  const notExpressions = new Set(['var', 'let', 'const', 'function', 'class', 'if', 'for', 'while', 'do', 'try', 'switch',
    'return', 'throw', 'break', 'continue', 'import', 'export', 'with', 'debugger']);
  statements.forEach(([from, to], index) => {
    const head = tokens[from];
    let end = to;
    if (is(tokens[end], ';')) end--;
    const text = end >= from ? src.slice(head.start, tokens[end].end) : '';
    const last = index === statements.length - 1;
    if ((is(head, 'var') || is(head, 'let') || is(head, 'const')) && tokens[from + 1] && !is(tokens[from + 1], '(') &&
        !is(tokens[from + 1], '=')) {
      // const a = 1, { b } = c  ->  void (a = 1, { b } = c)
      const assignments = [];
      let k = from + 1;
      while (k <= end) {
        // The pattern: a name, or a bracketed destructuring pattern
        const patternStart = k;
        if (is(tokens[k], '{') || is(tokens[k], '[')) {
          let level = 0;
          for (; k <= end; k++) {
            if (is(tokens[k], '{') || is(tokens[k], '[')) level++;
            else if ((is(tokens[k], '}') || is(tokens[k], ']')) && --level === 0) break;
          }
        } else if (!tokens[k] || tokens[k].type !== 'name') {
          fail();
        }
        const patternEnd = k;
        const names = [];
        patternNames(patternStart, patternEnd, names);
        hoisted.push(...names);
        k++;
        // The initializer, up to a comma at this level
        if (is(tokens[k], '=')) {
          let level = 0;
          let initEnd = k;
          for (k++; k <= end; k++) {
            const v = tokens[k].type === 'punct' ? tokens[k].value : '';
            if (v === '(' || v === '[' || v === '{') level++;
            else if (v === ')' || v === ']' || v === '}') level--;
            else if (v === ',' && level === 0) break;
            initEnd = k;
          }
          assignments.push(src.slice(tokens[patternStart].start, tokens[initEnd].end));
        }
        if (is(tokens[k], ',')) k++;
      }
      if (assignments.length > 0) body.push(`void (${assignments.join(', ')});`);
      if (last) body.push('return { value: undefined };');
      return;
    }
    if (is(head, 'function') || (is(head, 'async') && is(tokens[from + 1], 'function') && !tokens[from + 1].newline) || is(head, 'class')) {
      let k = from + 1;
      if (is(tokens[k], 'function')) k++;
      if (is(tokens[k], '*')) k++;
      if (tokens[k] && tokens[k].type === 'name' && !is(tokens[k], 'extends')) {
        hoisted.push(tokens[k].value);
        body.push(`${tokens[k].value} = ${text};`);
        if (last) body.push('return { value: undefined };');
        return;
      }
    }
    if (last && !notExpressions.has(head.value) && !is(head, '{') && !(is(head, 'async') && is(tokens[from + 1], 'function'))) {
      body.push(`return { value: (${text}) };`);
      return;
    }
    body.push(src.slice(head.start, tokens[to].end));
  });
  if (!body.some((line) => line.startsWith('return { value:'))) body.push('return { value: undefined };');
  const declarations = [...new Set(hoisted)].map((name) => `var ${name};`).join(' ');
  return `${declarations} (async () => { ${body.join('\n')} })()`;
})
)JS";

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
    bool awaiting = false;      // waiting for a top-level await; input waits too
    int await_id = 0;           // which await a settled promise belongs to
    std::string typeahead;      // terminal bytes typed while awaiting
    bool eof_pending = false;   // non-terminal input ended while awaiting
    bool editor_mode = false;   // .editor: lines are collected until Ctrl+D
    std::string editor_buffer;
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

void ResumeInput();

// "await" where a script can't have it: worth rewriting as an async function
bool NeedsAsyncWrap(TryCatch& try_catch) {
    std::string error = ToStdString(g_repl->isolate, try_catch.Exception());
    return error.find("await is only valid in async functions and the top level bodies of modules") != std::string::npos ||
           error.find("Unexpected reserved word") != std::string::npos;
}

void PrintUncaughtValue(Local<Context> context, Local<Value> exception) {
    Isolate* isolate = g_repl->isolate;
    context->Global()->Set(context, String::NewFromUtf8Literal(isolate, "_error"), exception).Check();
    PrintResult("Uncaught " + StripReplFrames(frogjs::Inspect(isolate, context, exception, g_repl->colors)));
}

// A settled top-level await: show its value or error, then take input again.
// The data is the await's id: one interrupted by Ctrl+C is ignored.
void OnAwaitSettled(const FunctionCallbackInfo<Value>& args, bool fulfilled) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    if (g_repl == nullptr || !g_repl->awaiting || frogjs::CallbackData(args)->Int32Value(context).FromMaybe(-1) != g_repl->await_id) return;
    g_repl->awaiting = false;
    if (fulfilled) {
        Local<Value> value = Undefined(isolate);
        if (args[0]->IsObject()) {
            TryCatch try_catch(isolate);
            if (!args[0].As<Object>()->Get(context, String::NewFromUtf8Literal(isolate, "value")).ToLocal(&value)) value = Undefined(isolate);
        }
        context->Global()->Set(context, String::NewFromUtf8Literal(isolate, "_"), value).Check();
        PrintResult(frogjs::Inspect(isolate, context, value, g_repl->colors));
    } else {
        PrintUncaughtValue(context, args[0]);
    }
    ResumeInput();
}

void OnAwaitFulfilled(const FunctionCallbackInfo<Value>& args) { OnAwaitSettled(args, true); }
void OnAwaitRejected(const FunctionCallbackInfo<Value>& args) { OnAwaitSettled(args, false); }

// Run input with top-level await. Returns false if it can't be rewritten (the
// original error is then shown).
bool EvaluateAsync(Local<Context> context, const std::string& code, const std::string& name) {
    Isolate* isolate = g_repl->isolate;
    std::string rewritten;
    {
        TryCatch try_catch(isolate);
        Local<Script> transform_script;
        Local<Value> transform;
        Local<Value> result;
        Local<Value> argv[1] = {String::NewFromUtf8(isolate, code.data(), NewStringType::kNormal, static_cast<int>(code.size())).ToLocalChecked()};
        if (!CompileInput(context, kTopLevelAwaitSource, "[await]").ToLocal(&transform_script) ||
            !transform_script->Run(context).ToLocal(&transform) || !transform->IsFunction() ||
            !transform.As<Function>()->Call(context, Undefined(isolate), 1, argv).ToLocal(&result) || !result->IsString()) {
            return false;
        }
        rewritten = ToStdString(isolate, result);
    }
    TryCatch try_catch(isolate);
    Local<Script> script;
    if (!CompileInput(context, rewritten, name).ToLocal(&script)) return false;
    RecordDeclarations(code);
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
        return true;
    }
    isolate->CancelTerminateExecution();
    if (!ok) {
        PrintException(context, try_catch);
        return true;
    }
    if (!result->IsPromise()) return true;
    g_repl->awaiting = true;
    Local<Value> id = Integer::New(isolate, ++g_repl->await_id);
    Local<Function> on_fulfilled = Function::New(context, OnAwaitFulfilled, id).ToLocalChecked();
    Local<Function> on_rejected = Function::New(context, OnAwaitRejected, id).ToLocalChecked();
    (void)result.As<Promise>()->Then(context, on_fulfilled, on_rejected);
    return true;
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
        if (NeedsAsyncWrap(try_catch) && EvaluateAsync(context, code, name)) return;
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
    } else if (command == ".editor" && g_repl->terminal) {
        // Lines are collected without running them; Ctrl+D runs them all
        g_repl->buffer.clear();
        g_repl->editor_mode = true;
        g_repl->editor_buffer.clear();
        PrintResult("// Entering editor mode (Ctrl+D to finish, Ctrl+C to cancel)");
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
    if (!g_repl->running || g_repl->awaiting) return;
    if (g_repl->terminal) {
        g_repl->editor->Start(g_repl->editor_mode ? "" : Prompt());
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

// .editor mode: Enter adds a line, Ctrl+D runs them, Ctrl+C drops them
bool FeedEditorMode(frogjs::LineEditor::Event event, const std::string& line) {
    switch (event) {
        case frogjs::LineEditor::Event::kLine:
            g_repl->editor_buffer += line + "\n";
            HidePrompt();
            ShowPrompt();
            return true;
        case frogjs::LineEditor::Event::kEof:
            HidePrompt();
            g_repl->editor_mode = false;
            g_repl->buffer = g_repl->editor_buffer;
            g_repl->session.push_back(g_repl->editor_buffer);
            g_repl->editor_buffer.clear();
            Evaluate();
            g_repl->buffer.clear();
            ShowPrompt();
            return true;
        case frogjs::LineEditor::Event::kInterrupt:
            HidePrompt();
            g_repl->editor_mode = false;
            g_repl->editor_buffer.clear();
            ShowPrompt();
            return true;
        case frogjs::LineEditor::Event::kNone:
            return false;
    }
    return false;
}

void FeedTerminal(unsigned char byte) {
    std::string line;
    frogjs::LineEditor::Event event = g_repl->editor->Feed(byte, &line);
    if (g_repl->editor_mode && FeedEditorMode(event, line)) return;
    switch (event) {
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
    while (g_repl->running && !g_repl->awaiting && (newline = g_repl->pending_input.find('\n')) != std::string::npos) {
        std::string line = g_repl->pending_input.substr(0, newline);
        g_repl->pending_input.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        HandleLine(line);
        ShowPrompt();
    }
    if (at_eof && g_repl->awaiting) {
        // Finish the await first (ResumeInput comes back here)
        g_repl->eof_pending = true;
        return;
    }
    if (at_eof && g_repl->running) {
        if (!g_repl->pending_input.empty()) {
            std::string line;
            line.swap(g_repl->pending_input);
            HandleLine(line);
            if (g_repl->awaiting) {
                g_repl->eof_pending = true;
                return;
            }
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
        for (ssize_t i = 0; i < n && g_repl->running; i++) {
            if (g_repl->awaiting) {
                // Typed while an await runs: kept for the next prompt
                g_repl->typeahead.append(chunk + i, static_cast<size_t>(n - i));
                break;
            }
            FeedTerminal(static_cast<unsigned char>(chunk[i]));
        }
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

// After an await settles: the prompt, then what was typed meanwhile
void ResumeInput() {
    if (!g_repl->running) return;
    ShowPrompt();
    if (g_repl->terminal) {
        std::string pending;
        pending.swap(g_repl->typeahead);
        for (size_t i = 0; i < pending.size() && g_repl->running; i++) {
            if (g_repl->awaiting) {
                g_repl->typeahead = pending.substr(i) + g_repl->typeahead;
                return;
            }
            FeedTerminal(static_cast<unsigned char>(pending[i]));
        }
    } else {
        bool eof = g_repl->eof_pending;
        g_repl->eof_pending = false;
        ProcessPendingLines(eof);
    }
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
    if (!g_repl->running) return;
    if (g_repl->awaiting) {
        // Stop waiting; the promise's result, when it comes, is ignored
        g_repl->awaiting = false;
        g_repl->typeahead.clear();
        PrintResult("Uncaught:\nScript execution was interrupted by `SIGINT`");
        ResumeInput();
        return;
    }
    if (g_repl->prompt_shown) FeedTerminal(3);
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
        // stdin can't be polled (e.g. a regular file): read it synchronously,
        // running the event loop while a top-level await waits
        while (state.running) {
            if (state.awaiting || state.eof_pending) {
                uv_run(loop, UV_RUN_ONCE);
                DrainTasks(isolate);
                if (!state.awaiting && !state.eof_pending) continue;
                if (state.awaiting && !uv_loop_alive(loop)) {
                    // Nothing left that could settle it
                    state.awaiting = false;
                    ResumeInput();
                }
                continue;
            }
            ReadStdin();
        }
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
