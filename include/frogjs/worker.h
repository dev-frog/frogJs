#pragma once

#include <v8.h>

namespace frogjs {

// Stop every running worker thread and wait for it to finish (before the main
// thread tears V8 down or exits)
void TerminateAllWorkers();

// process.exit() on a worker thread: ends that worker only
void ExitWorker(v8::Isolate* isolate, int exit_code);

// On a worker thread an uncaught error is not printed: it ends the worker and
// is emitted as the Worker's 'error' event in the parent. Returns false on the
// main thread.
bool ForwardWorkerError(v8::Isolate* isolate, v8::Local<v8::Value> exception);

}  // namespace frogjs
