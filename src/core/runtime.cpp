#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <uv.h>

using namespace v8;

// Forward declarations for binding setup functions
void SetupConsole(Isolate* isolate, Local<Context> context);
void SetupTimers(Isolate* isolate, Local<Context> context);
void SetupFileSystem(Isolate* isolate, Local<Context> context);
void SetupNet(Isolate* isolate, Local<Context> context);

// Read file contents into string
std::string ReadFile(const char* filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        exit(1);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Execute JavaScript code
void ExecuteScript(Isolate* isolate, Local<Context> context, const std::string& code, const char* filename) {
    // Create a string containing the JavaScript source code
    Local<String> source = String::NewFromUtf8(isolate, code.c_str()).ToLocalChecked();

    // Create a script origin (for better error messages)
    Local<String> name = String::NewFromUtf8(isolate, filename).ToLocalChecked();
    ScriptOrigin origin(name);

    // Compile the source code
    TryCatch try_catch(isolate);
    Local<Script> script;
    if (!Script::Compile(context, source, &origin).ToLocal(&script)) {
        // Print compilation errors
        String::Utf8Value error(isolate, try_catch.Exception());
        Local<Message> message = try_catch.Message();
        if (!message.IsEmpty()) {
            String::Utf8Value filename(isolate, message->GetScriptResourceName());
            int linenum = message->GetLineNumber(context).FromJust();
            fprintf(stderr, "%s:%d: %s\n", *filename, linenum, *error);
        } else {
            fprintf(stderr, "Error: %s\n", *error);
        }
        return;
    }

    // Run the script
    Local<Value> result;
    if (!script->Run(context).ToLocal(&result)) {
        // Print runtime errors
        String::Utf8Value error(isolate, try_catch.Exception());
        Local<Message> message = try_catch.Message();
        if (!message.IsEmpty()) {
            String::Utf8Value filename(isolate, message->GetScriptResourceName());
            int linenum = message->GetLineNumber(context).FromJust();
            fprintf(stderr, "%s:%d: %s\n", *filename, linenum, *error);

            // Print stack trace
            Local<Value> stack_trace_value;
            if (try_catch.StackTrace(context).ToLocal(&stack_trace_value)) {
                String::Utf8Value stack_trace(isolate, stack_trace_value);
                fprintf(stderr, "%s\n", *stack_trace);
            }
        } else {
            fprintf(stderr, "Error: %s\n", *error);
        }
        return;
    }
}

// console.log implementation
void ConsoleLog(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    for (int i = 0; i < args.Length(); i++) {
        if (i > 0) printf(" ");
        String::Utf8Value str(isolate, args[i]);
        printf("%s", *str);
    }
    printf("\n");
    fflush(stdout);
}

// console.error implementation
void ConsoleError(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    for (int i = 0; i < args.Length(); i++) {
        if (i > 0) fprintf(stderr, " ");
        String::Utf8Value str(isolate, args[i]);
        fprintf(stderr, "%s", *str);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

// Set up global console object
void SetupConsole(Isolate* isolate, Local<Context> context) {
    Local<Object> console = Object::New(isolate);

    // console.log
    console->Set(
        context,
        String::NewFromUtf8(isolate, "log").ToLocalChecked(),
        FunctionTemplate::New(isolate, ConsoleLog)->GetFunction(context).ToLocalChecked()
    ).Check();

    // console.error
    console->Set(
        context,
        String::NewFromUtf8(isolate, "error").ToLocalChecked(),
        FunctionTemplate::New(isolate, ConsoleError)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Attach console to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "console").ToLocalChecked(),
        console
    ).Check();
}

int main(int argc, char* argv[]) {
    // Check arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <script.js>\n", argv[0]);
        return 1;
    }

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

    {
        Isolate::Scope isolate_scope(isolate);
        HandleScope handle_scope(isolate);

        // Create a new context
        Local<Context> context = Context::New(isolate);
        Context::Scope context_scope(context);

        // Set up built-in bindings
        SetupConsole(isolate, context);
        SetupTimers(isolate, context);
        SetupFileSystem(isolate, context);
        SetupNet(isolate, context);

        // Read and execute the JavaScript file
        std::string code = ReadFile(argv[1]);
        ExecuteScript(isolate, context, code, argv[1]);

        // Run the event loop to handle async operations
        while (uv_run(uv_default_loop(), UV_RUN_DEFAULT)) {
            // Process V8 microtasks (Promises)
            isolate->PerformMicrotaskCheckpoint();
        }
    }

    // Dispose the isolate and tear down V8
    isolate->Dispose();
    V8::Dispose();
    V8::DisposePlatform();
    delete create_params.array_buffer_allocator;

    // Close the event loop
    uv_loop_close(uv_default_loop());

    return 0;
}
