#pragma once

#include <v8.h>

namespace frogjs {

// JavaScript coverage for `make coverage`: in a build with FROGJS_COVERAGE, the
// main thread records V8's precise coverage (from before the embedded modules
// are compiled) and writes it to $FROGJS_COVERAGE_DIR at exit. No-ops otherwise.
#ifdef FROGJS_COVERAGE
void EnableCoverage(v8::Isolate* isolate, v8::Local<v8::Context> context);
void WriteCoverage(v8::Isolate* isolate);
#else
inline void EnableCoverage(v8::Isolate*, v8::Local<v8::Context>) {}
inline void WriteCoverage(v8::Isolate*) {}
#endif

}  // namespace frogjs
