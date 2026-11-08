#pragma once

#include <v8.h>

namespace frogjs {

// process.emit(event, ...argv). Returns true if the event had listeners. Can
// throw (a listener's exception): call it inside a TryCatch.
bool EmitProcessEvent(v8::Isolate* isolate, v8::Local<v8::Context> context, const char* event,
                      int argc, v8::Local<v8::Value> argv[]);

// Set process.exitCode if needed, emit 'exit' once, and end the process (only
// the worker on a worker thread)
void ExitProcess(v8::Isolate* isolate, int code);

// The end of the program: emit 'exit' (once) and return the exit code to use,
// process.exitCode if a listener or the script set one, else code
int FinishProcess(v8::Isolate* isolate, v8::Local<v8::Context> context, int code);

// Forget the thread's process object (before its isolate is disposed)
void ResetProcessState();

}  // namespace frogjs
