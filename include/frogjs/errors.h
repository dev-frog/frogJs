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

}  // namespace frogjs
