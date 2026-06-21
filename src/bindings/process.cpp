#include <v8.h>
#include <uv.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>

using namespace v8;

// Global storage for argc/argv (needed for process.argv)
static int g_argc = 0;
static char** g_argv = nullptr;
static Isolate* g_isolate = nullptr;
static Persistent<Object> g_process_object;

// Store argc/argv for later use
void StoreCommandLineArgs(int argc, char* argv[]) {
    g_argc = argc;
    g_argv = argv;
}

// process.exit(code)
void ProcessExit(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    int exit_code = 0;
    if (args.Length() >= 1 && args[0]->IsNumber()) {
        exit_code = args[0]->Int32Value(isolate->GetCurrentContext()).ToChecked();
    }

    // Stop the event loop and exit
    uv_stop(uv_default_loop());
    exit(exit_code);
}

// process.cwd()
void ProcessCwd(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        args.GetReturnValue().Set(String::NewFromUtf8(isolate, cwd).ToLocalChecked());
    } else {
        isolate->ThrowException(String::NewFromUtf8(isolate, "Failed to get current working directory").ToLocalChecked());
    }
}

// Create process.argv array
Local<Array> CreateArgvArray(Isolate* isolate) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Array> argv_array = Array::New(isolate, g_argc);

    for (int i = 0; i < g_argc; i++) {
        if (g_argv[i] != nullptr) {
            argv_array->Set(context, i, String::NewFromUtf8(isolate, g_argv[i]).ToLocalChecked()).Check();
        } else {
            argv_array->Set(context, i, Null(isolate)).Check();
        }
    }

    return argv_array;
}

// Create process.env object
Local<Object> CreateEnvObject(Isolate* isolate) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> env_obj = Object::New(isolate);

    // Populate with environment variables
    extern char** environ;
    for (char** env = environ; *env != nullptr; env++) {
        char* equals = strchr(*env, '=');
        if (equals != nullptr) {
            int key_len = equals - *env;
            char* key = new char[key_len + 1];
            strncpy(key, *env, key_len);
            key[key_len] = '\0';

            Local<String> key_str = String::NewFromUtf8(isolate, key).ToLocalChecked();
            Local<String> value_str = String::NewFromUtf8(isolate, equals + 1).ToLocalChecked();

            env_obj->Set(context, key_str, value_str).Check();
            delete[] key;
        }
    }

    return env_obj;
}

// Get process.platform string
Local<String> GetPlatformString(Isolate* isolate) {
    const char* platform = "unknown";

#if defined(__APPLE__)
    platform = "darwin";
#elif defined(__linux__)
    platform = "linux";
#elif defined(_WIN32)
    platform = "win32";
#elif defined(__FreeBSD__)
    platform = "freebsd";
#elif defined(__OpenBSD__)
    platform = "openbsd";
#elif defined(__sun)
    platform = "sunos";
#elif defined(__AIX__)
    platform = "aix";
#endif

    return String::NewFromUtf8(isolate, platform).ToLocalChecked();
}

// Set up global process object
void SetupProcess(Isolate* isolate, Local<Context> context, int argc, char* argv[]) {
    // Store command-line arguments globally
    StoreCommandLineArgs(argc, argv);
    g_isolate = isolate;

    // Create process object
    Local<Object> process = Object::New(isolate);

    // process.exit(code)
    process->Set(
        context,
        String::NewFromUtf8(isolate, "exit").ToLocalChecked(),
        FunctionTemplate::New(isolate, ProcessExit)->GetFunction(context).ToLocalChecked()
    ).Check();

    // process.cwd()
    process->Set(
        context,
        String::NewFromUtf8(isolate, "cwd").ToLocalChecked(),
        FunctionTemplate::New(isolate, ProcessCwd)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Set up properties with their values
    // process.argv
    process->Set(
        context,
        String::NewFromUtf8(isolate, "argv").ToLocalChecked(),
        CreateArgvArray(isolate)
    ).Check();

    // process.env
    process->Set(
        context,
        String::NewFromUtf8(isolate, "env").ToLocalChecked(),
        CreateEnvObject(isolate)
    ).Check();

    // process.pid
    process->Set(
        context,
        String::NewFromUtf8(isolate, "pid").ToLocalChecked(),
        Integer::New(isolate, getpid())
    ).Check();

    // process.platform
    process->Set(
        context,
        String::NewFromUtf8(isolate, "platform").ToLocalChecked(),
        GetPlatformString(isolate)
    ).Check();

    // process.version
    process->Set(
        context,
        String::NewFromUtf8(isolate, "version").ToLocalChecked(),
        String::NewFromUtf8(isolate, "v0.2.0").ToLocalChecked()
    ).Check();

    // Store persistent reference
    g_process_object.Reset(isolate, process);

    // Attach process to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "process").ToLocalChecked(),
        process
    ).Check();
}
