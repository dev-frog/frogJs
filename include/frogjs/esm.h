#pragma once

#include <v8.h>

// ES modules: linking and evaluating module graphs with V8, for the loader in
// modules.cpp, which resolves specifiers and loads CommonJS.
namespace frogjs {

// Add the native functions the loader uses (containsModuleSyntax,
// importModule, requireModule, runMainModule) to `native`
void AddModuleNatives(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> native);

// The loader's functions (esmResolve, exportNames, syntheticExports, initMeta,
// importDynamic), and the isolate callbacks for import() and import.meta
void SetModuleLoader(v8::Isolate* isolate, v8::Local<v8::Object> loader);

// Whether source that doesn't compile as CommonJS is an ES module (import,
// export, import.meta, or top-level await)
bool ContainsModuleSyntax(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::String> source,
                          v8::Local<v8::Value> filename);

// After the event loop: if the main ES module is still waiting on a top-level
// await, print Node.js's warning and return true (the exit code is then 13)
bool ReportStalledMainModule(v8::Isolate* isolate);

// Forget every module (before the thread's isolate is disposed)
void ResetEsmState();

}  // namespace frogjs
