#pragma once

#include <v8.h>

namespace frogjs {

// Builds a context with every built-in binding installed (used by .clear)
using ContextFactory = v8::Local<v8::Context> (*)(v8::Isolate* isolate);

// Run the interactive shell on the default libuv loop until .exit, Ctrl+D or
// end of input. Timers and other async work keep running between prompts.
// Returns the process exit code.
int StartREPL(v8::Isolate* isolate, v8::Local<v8::Context> context, ContextFactory new_context);

// console.* call these around each write so output from async callbacks
// doesn't land in the middle of the line being edited
void ReplBeforeOutput();
void ReplAfterOutput();

}  // namespace frogjs
