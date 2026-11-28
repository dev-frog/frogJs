#pragma once

#include <v8.h>

namespace frogjs {

// Compile an embedded module source of the form `(function (...) { ... })` and
// call it with argv. Errors are reported to stderr as
// "Failed to initialize <name> module: ..." and leave the result empty.
v8::MaybeLocal<v8::Value> RunJsModule(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      const char* name, const char* source,
                                      int argc, v8::Local<v8::Value> argv[]);

// Set a native function as a property of target
void SetFunction(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target,
                 const char* name, v8::FunctionCallback callback);

// Built-in modules that require() knows but that aren't globals (url, assert,
// ...). They are kept on the global object under a private key, so scripts
// can't see or replace them. SetupModules adds them to require()'s table.
void RegisterBuiltin(v8::Isolate* isolate, v8::Local<v8::Context> context, const char* name,
                     v8::Local<v8::Value> value);
v8::Local<v8::Object> RegisteredBuiltins(v8::Isolate* isolate, v8::Local<v8::Context> context);

// A value of a module set up earlier: a global (fs, path, ...) or a registered
// built-in. Undefined if there is none.
v8::Local<v8::Value> GetBuiltin(v8::Isolate* isolate, v8::Local<v8::Context> context, const char* name);

}  // namespace frogjs
