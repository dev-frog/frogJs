#include <v8.h>
#include <uv.h>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <libgen.h>
#include <unistd.h>
#include <limits.h>

using namespace v8;

// Module cache: filepath -> exports object
std::map<std::string, Persistent<Object>> moduleCache;

// Get absolute path
std::string GetAbsolutePath(const std::string& path, const std::string& basePath) {
    if (path[0] == '/') {
        return path;
    }

    // Relative path - resolve relative to basePath
    char resolved[PATH_MAX];
    std::string fullPath = basePath + "/" + path;

    if (realpath(fullPath.c_str(), resolved)) {
        return std::string(resolved);
    }

    return fullPath;
}

// Read file contents
std::string ReadModuleFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Resolve module path (.js extension, index.js, etc.)
std::string ResolveModulePath(const std::string& path, const std::string& basePath) {
    std::string absPath = GetAbsolutePath(path, basePath);

    // Try exact path
    std::ifstream test(absPath);
    if (test.good()) {
        test.close();
        return absPath;
    }

    // Try with .js extension
    std::string withJs = absPath + ".js";
    test.open(withJs);
    if (test.good()) {
        test.close();
        return withJs;
    }

    // Try as directory with index.js
    std::string withIndex = absPath + "/index.js";
    test.open(withIndex);
    if (test.good()) {
        test.close();
        return withIndex;
    }

    return "";
}

// Get directory of a file path
std::string GetDirname(const std::string& filepath) {
    char* path = strdup(filepath.c_str());
    char* dir = dirname(path);
    std::string result(dir);
    free(path);
    return result;
}

// require() function implementation
void Require(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "require expects a string path").ToLocalChecked());
        return;
    }

    String::Utf8Value pathArg(isolate, args[0]);
    std::string requestedPath(*pathArg);

    // Get the caller's __dirname to resolve relative paths
    Local<Object> global = context->Global();
    Local<Value> dirnameVal = global->Get(context,
        String::NewFromUtf8(isolate, "__dirname").ToLocalChecked()).ToLocalChecked();

    std::string basePath = ".";
    if (dirnameVal->IsString()) {
        String::Utf8Value dirname(isolate, dirnameVal);
        basePath = *dirname;
    }

    // Resolve the module path
    std::string filepath = ResolveModulePath(requestedPath, basePath);

    if (filepath.empty()) {
        std::string error = "Cannot find module '" + requestedPath + "'";
        isolate->ThrowException(String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked());
        return;
    }

    // Check if module is already cached
    auto cached = moduleCache.find(filepath);
    if (cached != moduleCache.end()) {
        args.GetReturnValue().Set(Local<Object>::New(isolate, cached->second));
        return;
    }

    // Read module file
    std::string code = ReadModuleFile(filepath);
    if (code.empty()) {
        std::string error = "Failed to read module '" + filepath + "'";
        isolate->ThrowException(String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked());
        return;
    }

    // Create module object
    Local<Object> module = Object::New(isolate);
    Local<Object> exports = Object::New(isolate);
    module->Set(context, String::NewFromUtf8(isolate, "exports").ToLocalChecked(), exports).Check();

    // Get module directory
    std::string moduleDir = GetDirname(filepath);

    // Wrap module code in a function
    // (function(exports, require, module, __filename, __dirname) { ... })
    std::string wrappedCode =
        "(function(exports, require, module, __filename, __dirname) {\n" +
        code + "\n" +
        "})";

    // Compile the wrapped code
    Local<String> source = String::NewFromUtf8(isolate, wrappedCode.c_str()).ToLocalChecked();
    Local<String> filename = String::NewFromUtf8(isolate, filepath.c_str()).ToLocalChecked();
    ScriptOrigin origin(filename);

    TryCatch try_catch(isolate);
    Local<Script> script;
    if (!Script::Compile(context, source, &origin).ToLocal(&script)) {
        String::Utf8Value error(isolate, try_catch.Exception());
        Local<Message> message = try_catch.Message();
        if (!message.IsEmpty()) {
            String::Utf8Value filename(isolate, message->GetScriptResourceName());
            int linenum = message->GetLineNumber(context).FromJust();
            fprintf(stderr, "%s:%d: %s\n", *filename, linenum, *error);
        } else {
            fprintf(stderr, "Module compile error: %s\n", *error);
        }
        return;
    }

    // Run the script to get the wrapper function
    Local<Value> result;
    if (!script->Run(context).ToLocal(&result)) {
        String::Utf8Value error(isolate, try_catch.Exception());
        fprintf(stderr, "Module script error: %s\n", *error);
        return;
    }

    if (!result->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Module wrapper is not a function").ToLocalChecked());
        return;
    }

    // Call the wrapper function with module context
    Local<Function> wrapperFn = Local<Function>::Cast(result);

    // Get the require function
    Local<Value> requireFn = global->Get(context,
        String::NewFromUtf8(isolate, "require").ToLocalChecked()).ToLocalChecked();

    // Prepare arguments: exports, require, module, __filename, __dirname
    Local<Value> argv[5] = {
        exports,
        requireFn,
        module,
        String::NewFromUtf8(isolate, filepath.c_str()).ToLocalChecked(),
        String::NewFromUtf8(isolate, moduleDir.c_str()).ToLocalChecked()
    };

    // Execute the module
    if (!wrapperFn->Call(context, context->Global(), 5, argv).ToLocal(&result)) {
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
            fprintf(stderr, "Module execution error: %s\n", *error);
        }
        return;
    }

    // Get the final exports (might have been reassigned)
    Local<Value> finalExports = module->Get(context,
        String::NewFromUtf8(isolate, "exports").ToLocalChecked()).ToLocalChecked();

    if (!finalExports->IsObject()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Module exports must be an object").ToLocalChecked());
        return;
    }

    // Cache the module
    Local<Object> exportsObj = Local<Object>::Cast(finalExports);
    moduleCache[filepath].Reset(isolate, exportsObj);

    args.GetReturnValue().Set(exportsObj);
}

// Set up module system
void SetupModules(Isolate* isolate, Local<Context> context, const std::string& mainFilePath) {
    // Add require function to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "require").ToLocalChecked(),
        FunctionTemplate::New(isolate, Require)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Set __filename and __dirname for the main module
    std::string mainDir = GetDirname(mainFilePath);

    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "__filename").ToLocalChecked(),
        String::NewFromUtf8(isolate, mainFilePath.c_str()).ToLocalChecked()
    ).Check();

    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "__dirname").ToLocalChecked(),
        String::NewFromUtf8(isolate, mainDir.c_str()).ToLocalChecked()
    ).Check();
}
