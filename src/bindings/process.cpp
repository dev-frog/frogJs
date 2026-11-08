#include <v8.h>
#include <uv.h>
#include "frogjs/coverage.h"
#include "frogjs/errors.h"
#include "frogjs/js_module.h"
#include "frogjs/loop.h"
#include "frogjs/process.h"
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unistd.h>
#include <sys/types.h>
#include <string>
#include <vector>
#include "frogjs/version.h"
#include "frogjs/worker.h"

using namespace v8;

extern char** environ;

// The JavaScript half of process (and a few globals). It runs after every
// built-in exists, because process is an EventEmitter and stdin a stream.
static const char* kProcessSource = R"JS(
(function (process, native, EventEmitter, fs) {
'use strict';

function makeError(Ctor, code, message) {
  const err = new Ctor(message);
  err.code = code;
  return err;
}

function checkFunction(name, value) {
  if (typeof value !== 'function') {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
      `The "${name}" argument must be of type function. Received ${value === null ? 'null' : typeof value}`);
  }
}

Object.setPrototypeOf(process, EventEmitter.prototype);
EventEmitter.call(process);

// ---- Ending the process ----

process.exitCode = undefined;
process._exiting = false;

function emitExit(code) {
  if (process._exiting) return;
  process._exiting = true;
  if (process.exitCode === undefined) process.exitCode = code;
  try {
    process.emit('exit', process.exitCode);
  } catch (err) {
    console.error(err);
    process.exitCode = 7;
  }
}

process.exit = function exit(code) {
  if (code !== undefined) process.exitCode = code;
  emitExit(process.exitCode === undefined ? 0 : process.exitCode);
  native.reallyExit(Number(process.exitCode) || 0);
};

native.setExitFunctions(process.exit, (code) => {
  emitExit(code);
  return Number(process.exitCode) || 0;
});

// ---- Scheduling ----

// nextTick callbacks run before anything else queued later; they share the
// microtask queue with promises, so a promise reaction queued before the tick
// runs first (in Node.js ticks always go first)
const ticks = [];
let ticksScheduled = false;

function runTicks() {
  ticksScheduled = false;
  while (ticks.length > 0) {
    const tick = ticks.shift();
    try {
      tick.callback(...tick.args);
    } catch (err) {
      if (ticks.length > 0) schedule();
      throw err;
    }
  }
}

function schedule() {
  if (!ticksScheduled) {
    ticksScheduled = true;
    Promise.resolve().then(runTicks);
  }
}

process.nextTick = function nextTick(callback, ...args) {
  checkFunction('callback', callback);
  ticks.push({ callback, args });
  schedule();
};

globalThis.queueMicrotask = function queueMicrotask(callback) {
  checkFunction('callback', callback);
  Promise.resolve().then(() => callback());
};

// ---- Information ----

process.arch = native.arch;
process.title = 'frogjs';
process.release = { name: 'frogjs' };
process.execPath = native.execPath;
process.config = {};
process.features = {};

process.hrtime = function hrtime(previous) {
  const now = native.hrtime();
  const seconds = Math.floor(now / 1e9);
  const nanos = now - seconds * 1e9;
  if (previous === undefined) return [seconds, nanos];
  if (!Array.isArray(previous)) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "time" argument must be an instance of Array.');
  }
  if (previous.length !== 2) {
    throw makeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "time" is out of range. It must be 2. Received ${previous.length}`);
  }
  let diffSeconds = seconds - previous[0];
  let diffNanos = nanos - previous[1];
  if (diffNanos < 0) {
    diffSeconds--;
    diffNanos += 1e9;
  }
  return [diffSeconds, diffNanos];
};
process.hrtime.bigint = () => native.hrtimeBigInt();

const startTime = native.hrtime();
process.uptime = () => (native.hrtime() - startTime) / 1e9;
process.memoryUsage = () => native.memoryUsage();
process.memoryUsage.rss = () => native.memoryUsage().rss;
process.cpuUsage = () => native.cpuUsage();

process.chdir = function chdir(directory) {
  if (typeof directory !== 'string') {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "directory" argument must be of type string. Received ${typeof directory}`);
  }
  native.chdir(directory);
};

process.umask = (mask) => native.umask(mask === undefined ? -1 : typeof mask === 'string' ? parseInt(mask, 8) : mask);

process.kill = function kill(pid, signal) {
  if (!Number.isInteger(pid)) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "pid" argument must be of type number. Received ${typeof pid}`);
  }
  const signum = typeof signal === 'number' ? signal : native.signals[signal === undefined ? 'SIGTERM' : signal];
  if (signum === undefined) throw makeError(TypeError, 'ERR_UNKNOWN_SIGNAL', `Unknown signal: ${signal}`);
  native.kill(pid, signum);
  return true;
};

process.emitWarning = function emitWarning(warning, type, code) {
  const err = warning instanceof Error ? warning : new Error(warning);
  if (!(warning instanceof Error)) {
    if (typeof type === 'object' && type !== null) {
      code = type.code;
      type = type.type;
    }
    err.name = typeof type === 'string' ? type : 'Warning';
    if (code !== undefined) err.code = code;
  }
  process.nextTick(() => {
    if (process.listenerCount('warning') === 0) {
      console.error(`(frogjs:${process.pid}) ${err.code ? `[${err.code}] ` : ''}${err.name}: ${err.message}`);
    }
    process.emit('warning', err);
  });
};

// ---- Signals: process.on('SIGINT', ...) ----

function isSignal(event) {
  return typeof event === 'string' && Object.prototype.hasOwnProperty.call(native.signals, event);
}

if (native.isMainThread) {
  process.on('newListener', (event) => {
    if (isSignal(event) && process.listenerCount(event) === 0) {
      native.signalStart(native.signals[event], () => process.emit(event, event, native.signals[event]));
    }
  });
  process.on('removeListener', (event) => {
    if (isSignal(event) && process.listenerCount(event) === 0) native.signalStop(native.signals[event]);
  });
}

// ---- stdin: created on first use, because reading it keeps the process alive ----

let stdin;
Object.defineProperty(process, 'stdin', {
  configurable: true,
  enumerable: true,
  get() {
    if (!stdin) {
      stdin = new fs.ReadStream(null, { fd: 0, autoClose: false, highWaterMark: 64 * 1024 });
      stdin.isTTY = native.isatty0();
    }
    return stdin;
  },
});

// ---- Other globals ----

globalThis.global = globalThis;

const timeOrigin = Date.now();
const startHr = native.hrtime();
globalThis.performance = {
  timeOrigin,
  now: () => (native.hrtime() - startHr) / 1e6,
  toJSON: () => ({ timeOrigin }),
};

class TextEncoder {
  get encoding() {
    return 'utf-8';
  }
  encode(input = '') {
    return new Uint8Array(Buffer.from(String(input)));
  }
}

class TextDecoder {
  constructor(encoding = 'utf-8', options = {}) {
    const label = String(encoding).trim().toLowerCase();
    if (label !== 'utf-8' && label !== 'utf8') {
      throw makeError(RangeError, 'ERR_ENCODING_NOT_SUPPORTED', `The "${encoding}" encoding is not supported`);
    }
    this.encoding = 'utf-8';
    this.fatal = !!options.fatal;
    this.ignoreBOM = !!options.ignoreBOM;
  }
  decode(input) {
    if (input === undefined) return '';
    const bytes = ArrayBuffer.isView(input) ? Buffer.from(input.buffer, input.byteOffset, input.byteLength) : Buffer.from(input);
    let text = bytes.toString();
    if (this.fatal && text.includes('�') && !Buffer.from(text).equals(bytes)) {
      throw makeError(TypeError, 'ERR_ENCODING_INVALID_ENCODED_DATA', 'The encoded data was not valid for encoding utf-8');
    }
    if (!this.ignoreBOM && text.charCodeAt(0) === 0xFEFF) text = text.slice(1);
    return text;
  }
}

globalThis.TextEncoder = TextEncoder;
globalThis.TextDecoder = TextDecoder;
})
)JS";

namespace {

thread_local Global<Object> tls_process;
thread_local Global<Function> tls_exit_function;
thread_local Global<Function> tls_finish_function;

struct SignalWrap {
    uv_signal_t handle;
    Global<Function> callback;
    Isolate* isolate;
};

// Signal listeners exist only on the main thread, like in Node.js
std::map<int, SignalWrap*>* g_signals = nullptr;

void CloseSignal(SignalWrap* wrap) {
    uv_signal_stop(&wrap->handle);
    wrap->callback.Reset();
    uv_close(reinterpret_cast<uv_handle_t*>(&wrap->handle), [](uv_handle_t* handle) {
        delete static_cast<SignalWrap*>(handle->data);
    });
}

void CleanupSignals() {
    if (g_signals == nullptr) return;
    for (auto& entry : *g_signals) CloseSignal(entry.second);
    delete g_signals;
    g_signals = nullptr;
}

void SignalStart(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    int signum = args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(0);
    if (g_signals == nullptr) {
        g_signals = new std::map<int, SignalWrap*>;
        frogjs::AddThreadCleanup(CleanupSignals);
    }
    if (g_signals->count(signum) != 0) return;

    SignalWrap* wrap = new SignalWrap;
    wrap->isolate = isolate;
    wrap->callback.Reset(isolate, args[1].As<Function>());
    wrap->handle.data = wrap;
    uv_signal_init(frogjs::Loop(), &wrap->handle);
    // A signal listener alone doesn't keep the process alive
    uv_unref(reinterpret_cast<uv_handle_t*>(&wrap->handle));
    int status = uv_signal_start(&wrap->handle, [](uv_signal_t* handle, int) {
        SignalWrap* wrap = static_cast<SignalWrap*>(handle->data);
        Isolate* isolate = wrap->isolate;
        HandleScope handle_scope(isolate);
        (void)frogjs::MakeCallback(isolate, isolate->GetCurrentContext(), wrap->callback.Get(isolate),
                                   Undefined(isolate), 0, nullptr);  // reports its own errors
    }, signum);
    if (status != 0) {
        CloseSignal(wrap);
        isolate->ThrowException(frogjs::UVException(isolate, status, "uv_signal_start"));
        return;
    }
    (*g_signals)[signum] = wrap;
}

void SignalStop(const FunctionCallbackInfo<Value>& args) {
    int signum = args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
    if (g_signals == nullptr) return;
    auto it = g_signals->find(signum);
    if (it == g_signals->end()) return;
    CloseSignal(it->second);
    g_signals->erase(it);
}

// reallyExit(code): nothing more runs after this
void ReallyExit(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    int code = args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(0);
    if (frogjs::IsWorkerThread()) {
        frogjs::ExitWorker(isolate, code);
        return;
    }
    frogjs::WriteCoverage(isolate);
    frogjs::TerminateAllWorkers();
    fflush(stdout);
    exit(code);
}

void SetExitFunctions(const FunctionCallbackInfo<Value>& args) {
    tls_exit_function.Reset(args.GetIsolate(), args[0].As<Function>());
    tls_finish_function.Reset(args.GetIsolate(), args[1].As<Function>());
}

void ProcessCwd(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    char cwd[PATH_MAX];
    size_t size = sizeof(cwd);
    int status = uv_cwd(cwd, &size);
    if (status != 0) {
        isolate->ThrowException(frogjs::UVException(isolate, status, "uv_cwd"));
        return;
    }
    args.GetReturnValue().Set(String::NewFromUtf8(isolate, cwd).ToLocalChecked());
}

void Chdir(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    String::Utf8Value directory(isolate, args[0]);
    int status = uv_chdir(*directory);
    if (status != 0) isolate->ThrowException(frogjs::UVException(isolate, status, "chdir", *directory));
}

void Umask(const FunctionCallbackInfo<Value>& args) {
    int mask = args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(-1);
    mode_t previous;
    if (mask < 0) {
        previous = umask(0);
        umask(previous);
    } else {
        previous = umask(static_cast<mode_t>(mask));
    }
    args.GetReturnValue().Set(static_cast<int>(previous));
}

void Kill(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    int status = uv_kill(args[0]->Int32Value(context).FromMaybe(0), args[1]->Int32Value(context).FromMaybe(0));
    if (status != 0) isolate->ThrowException(frogjs::UVException(isolate, status, "kill"));
}

void Hrtime(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(static_cast<double>(uv_hrtime()));
}

void HrtimeBigInt(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(BigInt::NewFromUnsigned(args.GetIsolate(), uv_hrtime()));
}

void MemoryUsage(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HeapStatistics heap;
    isolate->GetHeapStatistics(&heap);
    size_t rss = 0;
    (void)uv_resident_set_memory(&rss);  // stays 0 if the OS can't tell
    Local<Object> usage = Object::New(isolate);
    auto set = [&](Local<String> name, double value) {
        usage->Set(context, name, Number::New(isolate, value)).Check();
    };
    set(String::NewFromUtf8Literal(isolate, "rss"), static_cast<double>(rss));
    set(String::NewFromUtf8Literal(isolate, "heapTotal"), static_cast<double>(heap.total_heap_size()));
    set(String::NewFromUtf8Literal(isolate, "heapUsed"), static_cast<double>(heap.used_heap_size()));
    set(String::NewFromUtf8Literal(isolate, "external"), static_cast<double>(heap.external_memory()));
    args.GetReturnValue().Set(usage);
}

void CpuUsage(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    uv_rusage_t usage;
    Local<Object> result = Object::New(isolate);
    double user = 0;
    double system = 0;
    if (uv_getrusage(&usage) == 0) {
        user = usage.ru_utime.tv_sec * 1e6 + usage.ru_utime.tv_usec;
        system = usage.ru_stime.tv_sec * 1e6 + usage.ru_stime.tv_usec;
    }
    result->Set(context, String::NewFromUtf8Literal(isolate, "user"), Number::New(isolate, user)).Check();
    result->Set(context, String::NewFromUtf8Literal(isolate, "system"), Number::New(isolate, system)).Check();
    args.GetReturnValue().Set(result);
}

void IsATty0(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(isatty(STDIN_FILENO) == 1);
}

// Create a JS array of strings (process.argv, process.execArgv)
Local<Array> CreateStringArray(Isolate* isolate, const std::vector<std::string>& values) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Array> array = Array::New(isolate, static_cast<int>(values.size()));
    for (size_t i = 0; i < values.size(); i++) {
        array->Set(context, static_cast<uint32_t>(i),
            String::NewFromUtf8(isolate, values[i].c_str()).ToLocalChecked()).Check();
    }
    return array;
}

Local<Object> CreateVersionsObject(Isolate* isolate) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> versions = Object::New(isolate);
    versions->Set(context, String::NewFromUtf8Literal(isolate, "frogjs"),
        String::NewFromUtf8(isolate, FROGJS_VERSION).ToLocalChecked()).Check();
    versions->Set(context, String::NewFromUtf8Literal(isolate, "v8"),
        String::NewFromUtf8(isolate, V8::GetVersion()).ToLocalChecked()).Check();
    versions->Set(context, String::NewFromUtf8Literal(isolate, "uv"),
        String::NewFromUtf8(isolate, uv_version_string()).ToLocalChecked()).Check();
    return versions;
}

// process.env: a plain object filled from the environment. Changes to it don't
// reach getenv(); child_process reads the object when spawning.
Local<Object> CreateEnvObject(Isolate* isolate) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> env_obj = Object::New(isolate);
    for (char** env = environ; *env != nullptr; env++) {
        const char* equals = strchr(*env, '=');
        if (equals == nullptr) continue;
        Local<String> key;
        Local<String> value;
        if (String::NewFromUtf8(isolate, *env, NewStringType::kNormal, static_cast<int>(equals - *env)).ToLocal(&key) &&
            String::NewFromUtf8(isolate, equals + 1).ToLocal(&value)) {
            env_obj->Set(context, key, value).Check();
        }
    }
    return env_obj;
}

const char* PlatformName() {
#if defined(__APPLE__)
    return "darwin";
#elif defined(__linux__)
    return "linux";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#else
    return "unknown";
#endif
}

const char* ArchName() {
#if defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(__x86_64__)
    return "x64";
#else
    return "unknown";
#endif
}

}  // namespace

// Also os.platform()
Local<String> GetPlatformString(Isolate* isolate) {
    return String::NewFromUtf8(isolate, PlatformName()).ToLocalChecked();
}

namespace frogjs {

bool EmitProcessEvent(Isolate* isolate, Local<Context> context, const char* event, int argc, Local<Value> argv[]) {
    if (tls_process.IsEmpty()) return false;
    HandleScope handle_scope(isolate);
    Local<Object> process = tls_process.Get(isolate);
    Local<Value> emit;
    if (!process->Get(context, String::NewFromUtf8Literal(isolate, "emit")).ToLocal(&emit) || !emit->IsFunction()) {
        return false;
    }
    std::vector<Local<Value>> args;
    args.push_back(String::NewFromUtf8(isolate, event).ToLocalChecked());
    for (int i = 0; i < argc; i++) args.push_back(argv[i]);
    Local<Value> result;
    if (!emit.As<Function>()->Call(context, process, static_cast<int>(args.size()), args.data()).ToLocal(&result)) {
        return false;
    }
    return result->IsTrue();
}

void ExitProcess(Isolate* isolate, int code) {
    if (!tls_exit_function.IsEmpty()) {
        HandleScope handle_scope(isolate);
        TryCatch try_catch(isolate);
        Local<Value> argv[1] = {Integer::New(isolate, code)};
        (void)tls_exit_function.Get(isolate)->Call(isolate->GetCurrentContext(), Undefined(isolate), 1, argv);
        // Only a worker gets here: its termination is pending
        if (IsWorkerThread()) return;
    }
    if (IsWorkerThread()) {
        ExitWorker(isolate, code);
        return;
    }
    WriteCoverage(isolate);
    TerminateAllWorkers();
    fflush(stdout);
    exit(code);
}

int FinishProcess(Isolate* isolate, Local<Context> context, int code) {
    if (tls_finish_function.IsEmpty()) return code;
    HandleScope handle_scope(isolate);
    TryCatch try_catch(isolate);
    Local<Value> argv[1] = {Integer::New(isolate, code)};
    Local<Value> result;
    if (!tls_finish_function.Get(isolate)->Call(context, Undefined(isolate), 1, argv).ToLocal(&result)) return code;
    return result->Int32Value(context).FromMaybe(code);
}

void ResetProcessState() {
    tls_exit_function.Reset();
    tls_finish_function.Reset();
    tls_process.Reset();
}

}  // namespace frogjs

// Set up the global process object. argv is [executable, script, ...script args]
// (no script entry for --eval and the REPL); exec_argv holds the FrogJS options.
// Runs early: other modules read process while they load.
void SetupProcess(Isolate* isolate, Local<Context> context, const std::vector<std::string>& argv,
                  const std::vector<std::string>& exec_argv) {
    Local<Object> process = Object::New(isolate);
    auto set = [&](Local<String> name, Local<Value> value) { process->Set(context, name, value).Check(); };

    frogjs::SetFunction(isolate, context, process, "cwd", ProcessCwd);
    set(String::NewFromUtf8Literal(isolate, "argv"), CreateStringArray(isolate, argv));
    set(String::NewFromUtf8Literal(isolate, "execArgv"), CreateStringArray(isolate, exec_argv));
    set(String::NewFromUtf8Literal(isolate, "env"), CreateEnvObject(isolate));
    set(String::NewFromUtf8Literal(isolate, "pid"), Integer::New(isolate, getpid()));
    set(String::NewFromUtf8Literal(isolate, "ppid"), Integer::New(isolate, getppid()));
    set(String::NewFromUtf8Literal(isolate, "platform"), String::NewFromUtf8(isolate, PlatformName()).ToLocalChecked());
    set(String::NewFromUtf8Literal(isolate, "version"), String::NewFromUtf8(isolate, "v" FROGJS_VERSION).ToLocalChecked());
    set(String::NewFromUtf8Literal(isolate, "versions"), CreateVersionsObject(isolate));

    tls_process.Reset(isolate, process);
    context->Global()->Set(context, String::NewFromUtf8Literal(isolate, "process"), process).Check();
}

// The JavaScript half (requires every built-in module)
void SetupProcessModule(Isolate* isolate, Local<Context> context) {
    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "reallyExit", ReallyExit);
    frogjs::SetFunction(isolate, context, native, "setExitFunctions", SetExitFunctions);
    frogjs::SetFunction(isolate, context, native, "chdir", Chdir);
    frogjs::SetFunction(isolate, context, native, "umask", Umask);
    frogjs::SetFunction(isolate, context, native, "kill", Kill);
    frogjs::SetFunction(isolate, context, native, "hrtime", Hrtime);
    frogjs::SetFunction(isolate, context, native, "hrtimeBigInt", HrtimeBigInt);
    frogjs::SetFunction(isolate, context, native, "memoryUsage", MemoryUsage);
    frogjs::SetFunction(isolate, context, native, "cpuUsage", CpuUsage);
    frogjs::SetFunction(isolate, context, native, "signalStart", SignalStart);
    frogjs::SetFunction(isolate, context, native, "signalStop", SignalStop);
    frogjs::SetFunction(isolate, context, native, "isatty0", IsATty0);
    native->Set(context, String::NewFromUtf8Literal(isolate, "arch"), String::NewFromUtf8(isolate, ArchName()).ToLocalChecked()).Check();
    native->Set(context, String::NewFromUtf8Literal(isolate, "isMainThread"), Boolean::New(isolate, !frogjs::IsWorkerThread())).Check();

    char exec_path[PATH_MAX];
    size_t exec_size = sizeof(exec_path);
    if (uv_exepath(exec_path, &exec_size) != 0) exec_path[0] = '\0';
    native->Set(context, String::NewFromUtf8Literal(isolate, "execPath"), String::NewFromUtf8(isolate, exec_path).ToLocalChecked()).Check();

    Local<Object> signals = Object::New(isolate);
    struct { const char* name; int value; } table[] = {
        {"SIGHUP", SIGHUP}, {"SIGINT", SIGINT}, {"SIGQUIT", SIGQUIT}, {"SIGILL", SIGILL}, {"SIGTRAP", SIGTRAP},
        {"SIGABRT", SIGABRT}, {"SIGFPE", SIGFPE}, {"SIGKILL", SIGKILL}, {"SIGUSR1", SIGUSR1}, {"SIGSEGV", SIGSEGV},
        {"SIGUSR2", SIGUSR2}, {"SIGPIPE", SIGPIPE}, {"SIGALRM", SIGALRM}, {"SIGTERM", SIGTERM}, {"SIGCHLD", SIGCHLD},
        {"SIGCONT", SIGCONT}, {"SIGSTOP", SIGSTOP}, {"SIGTSTP", SIGTSTP}, {"SIGWINCH", SIGWINCH},
    };
    for (const auto& entry : table) {
        signals->Set(context, String::NewFromUtf8(isolate, entry.name).ToLocalChecked(), Integer::New(isolate, entry.value)).Check();
    }
    native->Set(context, String::NewFromUtf8Literal(isolate, "signals"), signals).Check();

    Local<Object> global = context->Global();
    Local<Value> argv[4] = {
        tls_process.Get(isolate), native,
        global->Get(context, String::NewFromUtf8Literal(isolate, "events")).ToLocalChecked(),
        global->Get(context, String::NewFromUtf8Literal(isolate, "fs")).ToLocalChecked(),
    };
    (void)frogjs::RunJsModule(isolate, context, "process", kProcessSource, 4, argv);  // reports its own errors
}
