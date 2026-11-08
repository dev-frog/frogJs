#pragma once

#include <string>
#include <vector>
#include <v8.h>

namespace frogjs {

// Create a context with all built-in bindings installed. argv and exec_argv
// become process.argv and process.execArgv; main_filename is the main
// module's __filename.
v8::Local<v8::Context> CreateContext(v8::Isolate* isolate, const std::vector<std::string>& argv,
                                     const std::vector<std::string>& exec_argv,
                                     const std::string& main_filename);

// Compile and run a script. Uncaught errors are reported (see ReportException);
// returns false if one occurred.
bool ExecuteScript(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& code,
                   const std::string& filename, bool print_result = false, bool check_only = false);

// Load a file as the main module: wrapped like any module (top-level return,
// its own scope), with require.main set. Uncaught errors are handled as in
// ExecuteScript. Returns false if one ended the script.
bool RunMainModule(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& filename);

// Forget the module cache (before the thread's isolate is disposed)
void ResetModuleState();

// Read a script file. Returns false and sets *error to why it can't be read.
bool ReadScriptFile(const std::string& filename, std::string* out, std::string* error);

// Run the calling thread's event loop until it has nothing left to do
void RunEventLoop(v8::Isolate* isolate);

// The executable (argv[0]) and the FrogJS options of the main process, which
// workers inherit
const std::string& ExecPath();
const std::vector<std::string>& ExecArgv();

}  // namespace frogjs
