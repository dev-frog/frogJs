#include <v8.h>
#include <uv.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <string>
#include <vector>
#include "frogjs/version.h"

using namespace v8;

static Isolate* g_isolate = nullptr;
static Persistent<Object> g_process_object;

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

// Create a JS array of strings (process.argv, process.execArgv)
Local<Array> CreateStringArray(Isolate* isolate, const std::vector<std::string>& values) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Array> array = Array::New(isolate, static_cast<int>(values.size()));

    for (size_t i = 0; i < values.size(); i++) {
        array->Set(context, static_cast<uint32_t>(i),
            String::NewFromUtf8(isolate, values[i].c_str()).ToLocalChecked()).Check();
    }

    return array;
}

// Create process.versions object
Local<Object> CreateVersionsObject(Isolate* isolate) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> versions = Object::New(isolate);

    versions->Set(context, String::NewFromUtf8(isolate, "frogjs").ToLocalChecked(),
        String::NewFromUtf8(isolate, FROGJS_VERSION).ToLocalChecked()).Check();
    versions->Set(context, String::NewFromUtf8(isolate, "v8").ToLocalChecked(),
        String::NewFromUtf8(isolate, V8::GetVersion()).ToLocalChecked()).Check();
    versions->Set(context, String::NewFromUtf8(isolate, "uv").ToLocalChecked(),
        String::NewFromUtf8(isolate, uv_version_string()).ToLocalChecked()).Check();

    return versions;
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

// Set up global process object. argv is [executable, script, ...script args]
// (no script entry for --eval and the REPL); exec_argv holds the FrogJS options.
void SetupProcess(Isolate* isolate, Local<Context> context, const std::vector<std::string>& argv,
                  const std::vector<std::string>& exec_argv) {
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
        CreateStringArray(isolate, argv)
    ).Check();

    // process.execArgv
    process->Set(
        context,
        String::NewFromUtf8(isolate, "execArgv").ToLocalChecked(),
        CreateStringArray(isolate, exec_argv)
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
        String::NewFromUtf8(isolate, "v" FROGJS_VERSION).ToLocalChecked()
    ).Check();

    // process.versions
    process->Set(
        context,
        String::NewFromUtf8(isolate, "versions").ToLocalChecked(),
        CreateVersionsObject(isolate)
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
