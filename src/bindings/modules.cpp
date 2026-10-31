#include <v8.h>
#include <uv.h>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <libgen.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

using namespace v8;

// Module cache: filepath -> exports object (per thread: each worker has its own)
thread_local std::map<std::string, Persistent<Value>> moduleCache;

// Forget loaded modules (the REPL's .clear starts over with a fresh context)
void ClearModuleCache() {
    for (auto& entry : moduleCache) {
        entry.second.Reset();
    }
    moduleCache.clear();
}

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

// Canonical path of an existing regular file, or "" if there is none
static std::string ExistingFile(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
        return "";
    }
    char resolved[PATH_MAX];
    return realpath(path.c_str(), resolved) ? std::string(resolved) : path;
}

// Resolve module path (.js extension, index.js, etc.)
std::string ResolveModulePath(const std::string& path, const std::string& basePath) {
    std::string absPath = GetAbsolutePath(path, basePath);

    // Try exact path, then with .js extension, then as directory with index.js
    for (const std::string& candidate : {absPath, absPath + ".js", absPath + "/index.js"}) {
        std::string found = ExistingFile(candidate);
        if (!found.empty()) {
            return found;
        }
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
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate,
            "require expects a string path").ToLocalChecked()));
        return;
    }

    String::Utf8Value pathArg(isolate, args[0]);
    std::string requestedPath(*pathArg);

    Local<Object> global = context->Global();

    // Built-in modules are exposed as globals; require() returns the same object
    static const char* builtinModules[] = {"fs", "net", "http", "path", "os", "events", "stream", "util", "crypto",
                                           "child_process", "worker_threads"};
    for (const char* name : builtinModules) {
        if (requestedPath == name) {
            args.GetReturnValue().Set(global->Get(context,
                String::NewFromUtf8(isolate, name).ToLocalChecked()).ToLocalChecked());
            return;
        }
    }

    // Relative paths resolve from the requiring module's directory: each
    // module's require() carries it as its data; the main script's require()
    // uses the global __dirname
    Local<Value> dirnameVal = args.Data();
    if (!dirnameVal->IsString()) {
        dirnameVal = global->Get(context, String::NewFromUtf8(isolate, "__dirname").ToLocalChecked()).ToLocalChecked();
    }

    std::string basePath = ".";
    if (dirnameVal->IsString()) {
        String::Utf8Value dirname(isolate, dirnameVal);
        basePath = *dirname;
    }

    // Resolve the module path
    std::string filepath = ResolveModulePath(requestedPath, basePath);

    if (filepath.empty()) {
        std::string error = "Cannot find module '" + requestedPath + "'";
        isolate->ThrowException(Exception::Error(
            String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked()));
        return;
    }

    // Check if module is already cached
    auto cached = moduleCache.find(filepath);
    if (cached != moduleCache.end()) {
        args.GetReturnValue().Set(Local<Value>::New(isolate, cached->second));
        return;
    }

    // Read module file
    std::string code = ReadModuleFile(filepath);
    if (code.empty()) {
        std::string error = "Failed to read module '" + filepath + "'";
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked()));
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

    // Compile the wrapped code. The line offset hides the header line, so
    // line numbers in errors match the module file.
    Local<String> source = String::NewFromUtf8(isolate, wrappedCode.c_str()).ToLocalChecked();
    Local<String> filename = String::NewFromUtf8(isolate, filepath.c_str()).ToLocalChecked();
    ScriptOrigin origin(filename, -1);

    // Compile and run errors propagate to the caller of require(), which
    // reports them with the module's file and line if nothing catches them
    Local<Script> script;
    if (!Script::Compile(context, source, &origin).ToLocal(&script)) {
        return;
    }

    // Run the script to get the wrapper function
    Local<Value> result;
    if (!script->Run(context).ToLocal(&result)) {
        return;
    }

    if (!result->IsFunction()) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate,
            "Module wrapper is not a function").ToLocalChecked()));
        return;
    }

    // Call the wrapper function with module context
    Local<Function> wrapperFn = Local<Function>::Cast(result);

    // This module's require(), resolving relative to its directory
    Local<String> moduleDirStr = String::NewFromUtf8(isolate, moduleDir.c_str()).ToLocalChecked();
    Local<Value> requireFn =
        FunctionTemplate::New(isolate, Require, moduleDirStr)->GetFunction(context).ToLocalChecked();

    // Prepare arguments: exports, require, module, __filename, __dirname
    Local<Value> argv[5] = {
        exports, requireFn, module, String::NewFromUtf8(isolate, filepath.c_str()).ToLocalChecked(), moduleDirStr};

    // Execute the module
    if (!wrapperFn->Call(context, context->Global(), 5, argv).ToLocal(&result)) {
        return;
    }

    // Get the final exports (might have been reassigned)
    Local<Value> finalExports = module->Get(context,
        String::NewFromUtf8(isolate, "exports").ToLocalChecked()).ToLocalChecked();

    // Cache the module (exports can be any value, as in Node.js)
    moduleCache[filepath].Reset(isolate, finalExports);

    args.GetReturnValue().Set(finalExports);
}

// Set up module system
void SetupModules(Isolate* isolate, Local<Context> context, const std::string& mainFilePath) {
    // Add require function to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "require").ToLocalChecked(),
        FunctionTemplate::New(isolate, Require)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Set __filename and __dirname for the main module: absolute, as in
    // Node.js (pseudo names like "[eval]" stay as they are)
    std::string mainFile = mainFilePath;
    char resolved[PATH_MAX];
    if (!mainFile.empty() && mainFile[0] != '[' && realpath(mainFile.c_str(), resolved)) {
        mainFile = resolved;
    }
    std::string mainDir = GetDirname(mainFile);

    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "__filename").ToLocalChecked(),
        String::NewFromUtf8(isolate, mainFile.c_str()).ToLocalChecked()
    ).Check();

    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "__dirname").ToLocalChecked(),
        String::NewFromUtf8(isolate, mainDir.c_str()).ToLocalChecked()
    ).Check();

    // The main script isn't wrapped like other modules, but gets their
    // `module` and `exports` (CommonJS output of TypeScript assigns to them)
    Local<Object> mainModule = Object::New(isolate);
    Local<Object> mainExports = Object::New(isolate);
    mainModule->Set(context, String::NewFromUtf8(isolate, "exports").ToLocalChecked(), mainExports).Check();
    context->Global()->Set(context, String::NewFromUtf8(isolate, "module").ToLocalChecked(), mainModule).Check();
    context->Global()->Set(context, String::NewFromUtf8(isolate, "exports").ToLocalChecked(), mainExports).Check();
}
