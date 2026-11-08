#pragma once

#include <v8.h>

namespace frogjs {

// Print a caught exception to stderr in the Node.js style:
//
//   file.js:3
//       foo.bar();
//           ^
//
//   TypeError: Cannot read properties of undefined (reading 'bar')
//       at file.js:3:9
//
// The source excerpt is skipped when show_source is false (the REPL).
void ReportException(v8::Isolate* isolate, v8::TryCatch* try_catch, bool show_source = true);

// What every uncaught exception goes through (main script, callbacks, promises):
//  - termination (process.exit() in a worker, terminate()): ignored
//  - on a worker thread: ends the worker, 'error' event on the Worker
//  - process.on('uncaughtException') listeners: they get it and the process goes on
//  - in the REPL: printed
//  - otherwise: printed, 'exit' emitted, and the process exits with code 1
// Returns true if the program can continue.
bool HandleUncaught(v8::Isolate* isolate, v8::TryCatch* try_catch);
bool HandleUncaughtValue(v8::Isolate* isolate, v8::Local<v8::Value> exception);

// Call a JavaScript callback from the event loop. An exception goes through
// HandleUncaught. The result is empty if the callback threw.
v8::MaybeLocal<v8::Value> MakeCallback(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       v8::Local<v8::Function> callback, v8::Local<v8::Value> receiver,
                                       int argc, v8::Local<v8::Value> argv[]);

// An Error for a libuv status, like Node.js's fs errors:
// "ENOENT: no such file or directory, open 'x'", with errno, code, syscall,
// path and dest properties (path and dest may be null)
v8::Local<v8::Value> UVException(v8::Isolate* isolate, int status, const char* syscall,
                                 const char* path = nullptr, const char* dest = nullptr);

// The REPL prints uncaught errors instead of ending the process
void SetReplActive(bool active);

// Start tracking rejected promises nobody handles (once per isolate)
void TrackPromiseRejections(v8::Isolate* isolate);
// Run pending microtasks, then report the rejections still unhandled. The event
// loops call this after every iteration.
void DrainTasks(v8::Isolate* isolate);
// Drop tracked rejections (before the isolate is disposed)
void ClearPromiseRejections();

}  // namespace frogjs
