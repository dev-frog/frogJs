#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <uv.h>
#include "frogjs/errors.h"
#include "frogjs/repl.h"
#include "frogjs/util.h"
#include "frogjs/version.h"

using namespace v8;

// Forward declarations for binding setup functions
void SetupConsole(Isolate* isolate, Local<Context> context);
void SetupProcess(Isolate* isolate, Local<Context> context, const std::vector<std::string>& argv,
                  const std::vector<std::string>& exec_argv);
void SetupBuffer(Isolate* isolate, Local<Context> context);
void SetupTimers(Isolate* isolate, Local<Context> context);
void SetupFileSystem(Isolate* isolate, Local<Context> context);
void SetupNet(Isolate* isolate, Local<Context> context);
void SetupEvents(Isolate* isolate, Local<Context> context);
void SetupStream(Isolate* isolate, Local<Context> context);
void SetupHTTP(Isolate* isolate, Local<Context> context);
void SetupPath(Isolate* isolate, Local<Context> context);
void SetupOS(Isolate* isolate, Local<Context> context);
void SetupUtil(Isolate* isolate, Local<Context> context);
void SetupCrypto(Isolate* isolate, Local<Context> context);
void SetupModules(Isolate* isolate, Local<Context> context, const std::string& mainFilePath);

// Exit code for invalid command-line usage (same as Node.js)
static const int kBadOptionExitCode = 9;

static const char* kHelpText =
    "Usage: frogjs [options] [ script.js ] [arguments]\n"
    "       frogjs [options] -e \"script\" [arguments]\n"
    "       frogjs                          start the REPL\n"
    "\n"
    "Options:\n"
    "  -e, --eval <script>      evaluate script\n"
    "  -p, --print <script>     evaluate script and print the result\n"
    "  -c, --check              syntax check script without executing\n"
    "  -i, --interactive        start the REPL even if stdin is not a terminal\n"
    "                           (after running the script or --eval code, if any)\n"
    "  -h, --help               print this help message and exit\n"
    "  -v, --version            print the FrogJS version and exit\n"
    "  -                        read the script from stdin (the default when no\n"
    "                           script is given and stdin is not a terminal)\n"
    "  --                       end of FrogJS options; the next argument is the script\n"
    "\n"
    "Environment variables:\n"
    "  FROGJS_REPL_HISTORY      REPL history file (default ~/.frogjs_repl_history);\n"
    "                           set to an empty string to disable history\n"
    "  NO_COLOR                 disable colors in REPL output\n"
    "\n"
    "Documentation: https://github.com/dev-frog/frogJs\n";

struct Options {
    bool eval = false;         // -e / -p: run eval_code instead of a file
    bool print = false;        // -p: print the eval result
    bool check = false;        // -c
    bool interactive = false;  // -i
    bool help = false;
    bool version = false;
    std::string eval_code;
    std::string script;        // empty: no script given; "-": stdin
    std::vector<std::string> script_args;
    std::vector<std::string> exec_args;  // FrogJS options, for process.execArgv
};

// process.argv and the main module's __filename, used by CreateContext
static std::vector<std::string> g_process_argv;
static std::vector<std::string> g_exec_argv;
static std::string g_main_filename;

static int BadOption(const std::string& message) {
    fprintf(stderr, "frogjs: %s\n", message.c_str());
    fprintf(stderr, "Run 'frogjs --help' for usage.\n");
    return kBadOptionExitCode;
}

// Parse FrogJS options up to the script name; everything after it belongs to
// the script. Returns -1 on success, else the exit code.
static int ParseArgs(int argc, char* argv[], Options* opts) {
    int i = 1;
    for (; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--") {
            opts->exec_args.push_back(arg);
            i++;
            break;
        }
        if (arg.size() < 2 || arg[0] != '-') break;  // the script (or "-" for stdin)
        opts->exec_args.push_back(arg);

        bool takes_code = false;
        if (arg.rfind("--", 0) == 0) {
            std::string name = arg;
            std::string value;
            bool has_value = false;
            size_t equals = arg.find('=');
            if (equals != std::string::npos) {
                name = arg.substr(0, equals);
                value = arg.substr(equals + 1);
                has_value = true;
            }

            if (name == "--eval" || name == "--print") {
                opts->eval = true;
                opts->print = opts->print || name == "--print";
                if (has_value) {
                    opts->eval_code = value;
                } else {
                    takes_code = true;
                }
            } else if (has_value) {
                return BadOption(name + " does not take a value");
            } else if (name == "--check") {
                opts->check = true;
            } else if (name == "--interactive") {
                opts->interactive = true;
            } else if (name == "--help") {
                opts->help = true;
            } else if (name == "--version") {
                opts->version = true;
            } else {
                return BadOption("bad option: " + arg);
            }
        } else {
            // Short options can be combined: -pe "code", -ic
            for (size_t j = 1; j < arg.size(); j++) {
                switch (arg[j]) {
                    case 'p': opts->print = true; [[fallthrough]];
                    case 'e': opts->eval = true; takes_code = true; break;
                    case 'c': opts->check = true; break;
                    case 'i': opts->interactive = true; break;
                    case 'h': opts->help = true; break;
                    case 'v': opts->version = true; break;
                    default: return BadOption("bad option: " + arg);
                }
            }
        }

        if (takes_code) {
            if (i + 1 >= argc) return BadOption(arg + " requires an argument");
            opts->eval_code = argv[++i];
            opts->exec_args.push_back(opts->eval_code);
        }
    }

    // With --eval every positional argument is passed to the code
    if (!opts->eval && i < argc) opts->script = argv[i++];
    for (; i < argc; i++) opts->script_args.push_back(argv[i]);

    if (opts->check && opts->eval) return BadOption("either --check or --eval can be used, not both");
    return -1;
}

// Read a script file. Returns false and prints why if it can't be read.
static bool ReadScriptFile(const std::string& filename, std::string* out) {
    struct stat info;
    if (stat(filename.c_str(), &info) != 0) {
        fprintf(stderr, "frogjs: cannot open '%s': %s\n", filename.c_str(), strerror(errno));
        return false;
    }
    if (S_ISDIR(info.st_mode)) {
        fprintf(stderr, "frogjs: cannot run '%s': is a directory\n", filename.c_str());
        return false;
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        fprintf(stderr, "frogjs: cannot open '%s': %s\n", filename.c_str(), strerror(errno));
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    *out = buffer.str();
    return true;
}

// Compile and run the main script (a file, --eval code or stdin). Uncaught
// errors are reported Node.js style; returns false if one occurred.
static bool ExecuteScript(Isolate* isolate, Local<Context> context, const std::string& code,
                          const std::string& filename, bool print_result, bool check_only) {
    HandleScope handle_scope(isolate);
    TryCatch try_catch(isolate);

    Local<String> source;
    if (!String::NewFromUtf8(isolate, code.c_str(), NewStringType::kNormal,
            static_cast<int>(code.size())).ToLocal(&source)) {
        fprintf(stderr, "frogjs: '%s' is too large to load\n", filename.c_str());
        return false;
    }
    ScriptOrigin origin(String::NewFromUtf8(isolate, filename.c_str()).ToLocalChecked());

    Local<Script> script;
    if (!Script::Compile(context, source, &origin).ToLocal(&script)) {
        frogjs::ReportException(isolate, &try_catch);
        return false;
    }
    if (check_only) return true;

    Local<Value> result;
    if (!script->Run(context).ToLocal(&result)) {
        if (try_catch.HasCaught()) frogjs::ReportException(isolate, &try_catch);
        return false;
    }

    // Printed the way console.log would: strings as-is, anything else inspected
    if (print_result) {
        std::string text;
        if (result->IsString()) {
            String::Utf8Value str(isolate, result);
            text.assign(*str, str.length());
        } else {
            text = frogjs::Inspect(isolate, context, result);
        }
        fwrite(text.data(), 1, text.size(), stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
    return true;
}

// Write console output: util.format(...args) plus a newline
static void WriteConsole(const FunctionCallbackInfo<Value>& args, FILE* stream) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    std::string text;
    if (!frogjs::Format(isolate, isolate->GetCurrentContext(), args, &text)) return;
    text += '\n';

    frogjs::ReplBeforeOutput();
    fwrite(text.data(), 1, text.size(), stream);
    fflush(stream);
    frogjs::ReplAfterOutput();
}

// console.log / console.info / console.debug implementation
void ConsoleLog(const FunctionCallbackInfo<Value>& args) {
    WriteConsole(args, stdout);
}

// console.error / console.warn implementation
void ConsoleError(const FunctionCallbackInfo<Value>& args) {
    WriteConsole(args, stderr);
}

// Set up global console object
void SetupConsole(Isolate* isolate, Local<Context> context) {
    Local<Object> console = Object::New(isolate);

    struct { const char* name; FunctionCallback callback; } methods[] = {
        {"log", ConsoleLog},
        {"info", ConsoleLog},
        {"debug", ConsoleLog},
        {"error", ConsoleError},
        {"warn", ConsoleError},
    };
    for (const auto& method : methods) {
        console->Set(
            context,
            String::NewFromUtf8(isolate, method.name).ToLocalChecked(),
            FunctionTemplate::New(isolate, method.callback)->GetFunction(context).ToLocalChecked()
        ).Check();
    }

    // Attach console to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "console").ToLocalChecked(),
        console
    ).Check();
}

// Create a context with all built-in bindings installed
static Local<Context> CreateContext(Isolate* isolate) {
    EscapableHandleScope handle_scope(isolate);
    Local<Context> context = Context::New(isolate);
    Context::Scope context_scope(context);

    SetupConsole(isolate, context);
    SetupUtil(isolate, context);
    SetupProcess(isolate, context, g_process_argv, g_exec_argv);
    SetupBuffer(isolate, context);
    SetupTimers(isolate, context);
    SetupFileSystem(isolate, context);
    SetupNet(isolate, context);
    SetupEvents(isolate, context);
    SetupStream(isolate, context);
    SetupHTTP(isolate, context);
    SetupCrypto(isolate, context);
    SetupPath(isolate, context);
    SetupOS(isolate, context);
    SetupModules(isolate, context, g_main_filename);

    return handle_scope.Escape(context);
}

int main(int argc, char* argv[]) {
    Options opts;
    int parse_result = ParseArgs(argc, argv, &opts);
    if (parse_result >= 0) return parse_result;

    if (opts.help) {
        fputs(kHelpText, stdout);
        return 0;
    }
    if (opts.version) {
        printf("v%s\n", FROGJS_VERSION);
        return 0;
    }

    // Work out what to run: --eval code, a script file, stdin, or nothing (REPL)
    std::string code;
    bool has_code = true;
    g_process_argv.push_back(argv[0]);
    if (opts.eval) {
        code = opts.eval_code;
        g_main_filename = "[eval]";
    } else if (!opts.script.empty() && opts.script != "-") {
        if (!ReadScriptFile(opts.script, &code)) return 1;
        g_main_filename = opts.script;
        g_process_argv.push_back(opts.script);
    } else if (opts.script == "-" || !isatty(STDIN_FILENO)) {
        if (opts.script == "-") g_process_argv.push_back("-");
        if (opts.script == "-" || !opts.interactive) {
            std::stringstream buffer;
            buffer << std::cin.rdbuf();
            code = buffer.str();
            g_main_filename = "[stdin]";
        } else {
            has_code = false;  // -i with piped input: the REPL reads stdin
            g_main_filename = "[repl]";
        }
    } else {
        has_code = false;
        g_main_filename = "[repl]";
    }
    g_process_argv.insert(g_process_argv.end(), opts.script_args.begin(), opts.script_args.end());
    g_exec_argv = opts.exec_args;

    if (opts.check && !has_code) {
        return BadOption("--check needs a script file or stdin");
    }
    bool start_repl = opts.interactive || (!has_code && !opts.check);

    // Initialize V8
    V8::InitializeICUDefaultLocation(argv[0]);
    V8::InitializeExternalStartupData(argv[0]);
    std::unique_ptr<Platform> platform = platform::NewDefaultPlatform();
    V8::InitializePlatform(platform.get());
    V8::Initialize();

    // Create a new Isolate and make it the current one
    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = ArrayBuffer::Allocator::NewDefaultAllocator();
    Isolate* isolate = Isolate::New(create_params);

    int exit_code = 0;
    {
        Isolate::Scope isolate_scope(isolate);
        HandleScope handle_scope(isolate);

        // Create a new context with the built-in bindings
        Local<Context> context = CreateContext(isolate);
        Context::Scope context_scope(context);

        // Like Node.js, an uncaught error in the main script ends the process
        // without running the event loop
        if (has_code && !ExecuteScript(isolate, context, code, g_main_filename, opts.print, opts.check)) {
            exit_code = 1;
        } else if (start_repl) {
            exit_code = frogjs::StartREPL(isolate, context, CreateContext);
        } else if (!opts.check) {
            // Run the event loop to handle async operations
            while (uv_run(uv_default_loop(), UV_RUN_DEFAULT)) {
                // Process V8 microtasks (Promises)
                isolate->PerformMicrotaskCheckpoint();
            }
        }
    }

    // Dispose the isolate and tear down V8
    isolate->Dispose();
    V8::Dispose();
    V8::DisposePlatform();
    delete create_params.array_buffer_allocator;

    // Close the event loop
    uv_loop_close(uv_default_loop());

    return exit_code;
}
