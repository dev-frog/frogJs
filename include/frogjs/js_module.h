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

}  // namespace frogjs
