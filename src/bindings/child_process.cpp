#include <v8.h>
#include <uv.h>
#include <csignal>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "frogjs/buffer.h"
#include "frogjs/encoding.h"
#include "frogjs/errors.h"
#include "frogjs/js_module.h"
#include "frogjs/loop.h"
#include "frogjs/v8_compat.h"

using namespace v8;

// child_process: spawn() runs a process on the event loop (uv_spawn) with its
// stdio as streams; exec/execFile buffer the output. The *Sync variants run
// the child on a private loop and block until it is done.
static const char* kChildProcessSource = R"JS(
(function (native, EventEmitter, stream, Buffer, path) {
'use strict';

const { Readable, Writable } = stream;
const signals = native.signals;
const signalNames = Object.create(null);
for (const name of Object.keys(signals)) signalNames[signals[name]] = name;

// Captured now: scripts can replace the global
const scheduleImmediate = setImmediate;

function nextTick(fn, ...args) {
  Promise.resolve().then(() => fn(...args));
}

function makeError(Ctor, code, message) {
  const err = new Ctor(message);
  err.code = code;
  return err;
}

function describe(value) {
  if (value === null || value === undefined) return String(value);
  if (typeof value === 'function') return 'function ' + (value.name || '<anonymous>');
  if (typeof value === 'object') return 'an instance of ' + ((value.constructor && value.constructor.name) || 'Object');
  let shown = typeof value === 'string' ? `'${value}'` : String(value);
  if (shown.length > 28) shown = shown.slice(0, 25) + '...';
  return `type ${typeof value} (${shown})`;
}

function invalidArgType(name, expected, value) {
  return makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
    `The "${name}" argument must be ${expected}. Received ${describe(value)}`);
}

// A libuv error as Node.js reports it: "spawn ls ENOENT"
function errnoException(errno, syscall) {
  const code = native.errname(errno);
  const err = new Error(`${syscall} ${code}`);
  err.errno = errno;
  err.code = code;
  err.syscall = syscall;
  return err;
}

function convertSignal(signal) {
  if (typeof signal === 'number' && signalNames[signal] !== undefined) return signal;
  if (typeof signal === 'string' && signals[signal.toUpperCase()] !== undefined) return signals[signal.toUpperCase()];
  throw makeError(TypeError, 'ERR_UNKNOWN_SIGNAL', 'Unknown signal: ' + signal);
}

function validateTimeout(timeout) {
  if (timeout !== undefined && timeout !== null && (!Number.isInteger(timeout) || timeout < 0)) {
    throw makeError(RangeError, 'ERR_OUT_OF_RANGE',
      `The value of "timeout" is out of range. It must be an unsigned integer. Received ${timeout}`);
  }
}

function validateMaxBuffer(maxBuffer) {
  if (maxBuffer !== undefined && maxBuffer !== null && maxBuffer !== Infinity &&
      (typeof maxBuffer !== 'number' || !(maxBuffer >= 0))) {
    throw makeError(RangeError, 'ERR_OUT_OF_RANGE',
      `The value of "options.maxBuffer" is out of range. It must be a positive number. Received ${maxBuffer}`);
  }
}

// (file, args, options) -> what native.spawn() needs
function normalizeSpawnArguments(file, args, options) {
  if (typeof file !== 'string') throw invalidArgType('file', 'of type string', file);
  if (file.length === 0) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE', "The argument 'file' cannot be empty. Received ''");
  }
  if (Array.isArray(args)) {
    args = args.slice();
  } else if (args === undefined || args === null) {
    args = [];
  } else if (typeof args === 'object') {
    options = args;
    args = [];
  } else {
    throw invalidArgType('args', 'of type object', args);
  }
  if (options === undefined) options = {};
  if (options === null || typeof options !== 'object') throw invalidArgType('options', 'of type object', options);

  let cwd = options.cwd;
  if (cwd !== undefined && cwd !== null) {
    if (typeof cwd === 'object' && typeof cwd.href === 'string') cwd = decodeURIComponent(cwd.pathname);
    if (typeof cwd !== 'string') throw invalidArgType('options.cwd', 'of type string or an instance of URL', cwd);
  } else {
    cwd = undefined;
  }

  // C strings end at a null byte: a truncated argument isn't what the caller checked
  const checkNoNull = (name, value) => {
    if (typeof value === 'string' && value.includes('\0')) {
      throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE',
        `The argument '${name}' must be a string without null bytes. Received ${JSON.stringify(value)}`);
    }
  };
  checkNoNull('file', file);
  checkNoNull('options.cwd', cwd);
  args.forEach((arg, i) => checkNoNull(`args[${i}]`, arg));

  if (options.shell) {
    const command = [file, ...args].join(' ');
    file = typeof options.shell === 'string' ? options.shell : '/bin/sh';
    args = ['-c', command];
  }
  if (options.argv0 !== undefined && typeof options.argv0 !== 'string') {
    throw invalidArgType('options.argv0', 'of type string', options.argv0);
  }

  const env = options.env || process.env;
  const envPairs = [];
  for (const key of Object.keys(env)) {
    if (env[key] !== undefined) {
      const pair = `${key}=${env[key]}`;
      checkNoNull('options.env', pair);
      envPairs.push(pair);
    }
  }

  return {
    ...options,
    file,
    args: [options.argv0 || file, ...args],
    cwd,
    envPairs,
    detached: !!options.detached,
  };
}

// stdio option -> [{ type: 'pipe' | 'ignore' | 'fd', fd }]
function normalizeStdio(stdio) {
  if (stdio === undefined || stdio === null) stdio = 'pipe';
  if (typeof stdio === 'string') {
    if (!['pipe', 'overlapped', 'inherit', 'ignore'].includes(stdio)) {
      throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'stdio' is invalid. Received '${stdio}'`);
    }
    stdio = [stdio, stdio, stdio];
  } else if (!Array.isArray(stdio)) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'stdio' is invalid. Received ${describe(stdio)}`);
  }
  stdio = stdio.slice();
  while (stdio.length < 3) stdio.push(undefined);
  return stdio.map((entry, i) => {
    if (entry === undefined || entry === null) entry = i < 3 ? 'pipe' : 'ignore';
    if (entry === 'pipe' || entry === 'overlapped') return { type: 'pipe' };
    if (entry === 'ignore') return { type: 'ignore' };
    if (entry === 'inherit') return { type: 'fd', fd: i };
    if (typeof entry === 'number' && Number.isInteger(entry) && entry >= 0) return { type: 'fd', fd: entry };
    if (entry && typeof entry === 'object' && typeof entry.fd === 'number') return { type: 'fd', fd: entry.fd };
    if (entry === 'ipc') {
      throw makeError(Error, 'ERR_FEATURE_UNAVAILABLE_ON_PLATFORM', 'IPC channels are not supported by FrogJS');
    }
    throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'stdio' is invalid. Received ${describe(entry)}`);
  });
}

// The parent's end of a pipe as a stream: readable for the child's output,
// writable for its input
function readablePipe(pipe) {
  const readable = new Readable({
    read() { if (pipe) native.readStart(pipe); },
    destroy(err, callback) {
      closePipe();
      callback(err);
    },
  });
  function closePipe() {
    if (!pipe) return;
    const p = pipe;
    pipe = null;
    native.closePipe(p);
  }
  pipe.onread = (data) => {
    if (data === null) {
      readable.push(null);
      // Like a net.Socket the stream ends (and closes) even if nobody reads it
      if (readable._readableState.flowing === null) readable.resume();
      closePipe();
    } else if (typeof data === 'number') {
      readable.destroy(errnoException(data, 'read'));
    } else if (!readable.push(data)) {
      native.readStop(pipe);
    }
  };
  // Read eagerly like a net.Socket, so EOF (and 'close') is seen unconsumed
  native.readStart(pipe);
  return readable;
}

function writablePipe(pipe) {
  const writable = new Writable({
    write(chunk, encoding, callback) {
      if (!pipe) return callback(makeError(Error, 'ERR_STREAM_DESTROYED', 'Cannot call write after a stream was destroyed'));
      if (typeof chunk === 'string') chunk = Buffer.from(chunk, encoding);
      const err = native.write(pipe, chunk, (status) => callback(status < 0 ? errnoException(status, 'write') : null));
      if (err < 0) callback(errnoException(err, 'write'));
    },
    final(callback) {
      if (!pipe) return callback();
      native.shutdown(pipe, () => {
        closePipe();
        callback();
      });
    },
    destroy(err, callback) {
      closePipe();
      callback(err);
    },
  });
  function closePipe() {
    if (!pipe) return;
    const p = pipe;
    pipe = null;
    native.closePipe(p);
  }
  return writable;
}

// A stream for a child that never started: ends (or finishes) right away
function deadStream(readable) {
  const s = readable ? new Readable({ read() {} }) : new Writable({ write(c, e, cb) { cb(); } });
  if (readable) {
    s.push(null);
    s.resume();
  } else {
    nextTick(() => s.destroy());
  }
  return s;
}

class ChildProcess extends EventEmitter {
  constructor() {
    super();
    this._closesNeeded = 1;
    this._closesGot = 0;
    this.connected = false;
    this.signalCode = null;
    this.exitCode = null;
    this.killed = false;
    this.spawnfile = null;
    this._handle = null;
    this.spawnargs = [];
    this.pid = undefined;
    this.stdin = null;
    this.stdout = null;
    this.stderr = null;
    this.stdio = [];
  }

  spawn(options) {
    if (options === null || typeof options !== 'object') throw invalidArgType('options', 'of type object', options);
    const stdio = normalizeStdio(options.stdio);
    this.spawnfile = options.file;
    this.spawnargs = options.args;

    const result = native.spawn({ ...options, stdio });
    const failed = typeof result === 'number';

    // 'close' follows 'exit' and the 'close' of every stdio stream
    stdio.forEach((entry, i) => {
      let s = null;
      if (entry.type === 'pipe') {
        if (failed) s = deadStream(i !== 0);
        else if (i === 0) s = writablePipe(result.stdio[i]);
        else s = readablePipe(result.stdio[i]);
        this._closesNeeded++;
        s.on('close', () => this._maybeClose());
      }
      this.stdio.push(s);
    });
    this.stdin = this.stdio[0] || null;
    this.stdout = this.stdio[1] || null;
    this.stderr = this.stdio[2] || null;

    if (failed) {
      const err = errnoException(result, 'spawn ' + this.spawnfile);
      err.path = this.spawnfile;
      err.spawnargs = this.spawnargs.slice(1);
      nextTick(() => {
        this.emit('error', err);
        this.exitCode = result;
        // 'close' comes on a later turn of the event loop, so code that reacts
        // to 'error' can still add a listener for it (as in Node.js)
        scheduleImmediate(() => this._maybeClose());
      });
      return result;
    }

    this._handle = result;
    this.pid = result.pid;
    result.onexit = (exitCode, signal) => {
      if (signal) this.signalCode = signalNames[signal] || signal;
      else this.exitCode = exitCode;
      this._handle = null;
      if (this.stdin) this.stdin.destroy();
      this.emit('exit', this.exitCode, this.signalCode);
      this._maybeClose();
    };
    nextTick(() => this.emit('spawn'));
    return 0;
  }

  _maybeClose() {
    if (++this._closesGot === this._closesNeeded) this.emit('close', this.exitCode, this.signalCode);
  }

  kill(signal) {
    const signum = signal === 0 ? 0 : convertSignal(signal === undefined ? 'SIGTERM' : signal);
    if (!this._handle) return false;
    const err = native.kill(this._handle, signum);
    if (err === 0) {
      this.killed = true;
      return true;
    }
    if (err !== native.errno.ESRCH) this.emit('error', errnoException(err, 'kill'));
    return false;
  }

  ref() { if (this._handle) native.ref(this._handle, true); }
  unref() { if (this._handle) native.ref(this._handle, false); }

  [Symbol.dispose]() { if (!this.killed) this.kill(); }
}

function spawn(file, args, options) {
  const opts = normalizeSpawnArguments(file, args, options);
  validateTimeout(opts.timeout);
  const killSignal = opts.killSignal === undefined ? 'SIGTERM' : opts.killSignal;
  if (opts.timeout > 0) convertSignal(killSignal);

  const child = new ChildProcess();
  child.spawn(opts);

  if (opts.timeout > 0) {
    let timer = setTimeout(() => {
      timer = null;
      try { child.kill(killSignal); } catch (err) { child.emit('error', err); }
    }, opts.timeout);
    child.once('exit', () => { if (timer) clearTimeout(timer); });
  }
  return child;
}

// exec(command[, options][, callback])
function normalizeExecArgs(command, options, callback) {
  if (typeof command !== 'string') throw invalidArgType('command', 'of type string', command);
  if (typeof options === 'function') {
    callback = options;
    options = undefined;
  }
  options = { ...options };
  options.shell = typeof options.shell === 'string' ? options.shell : true;
  return { file: command, options, callback };
}

function exec(command, options, callback) {
  const opts = normalizeExecArgs(command, options, callback);
  return execFile(opts.file, opts.options, opts.callback);
}

// execFile(file[, args][, options][, callback])
function execFile(file, args, options, callback) {
  if (typeof args === 'function') {
    callback = args;
    args = undefined;
    options = undefined;
  } else if (args !== null && typeof args === 'object' && !Array.isArray(args)) {
    callback = options;
    options = args;
    args = undefined;
  } else if (typeof options === 'function') {
    callback = options;
    options = undefined;
  }
  if (callback !== undefined && callback !== null && typeof callback !== 'function') {
    throw invalidArgType('callback', 'of type function', callback);
  }

  options = {
    encoding: 'utf8',
    timeout: 0,
    maxBuffer: 1024 * 1024,
    killSignal: 'SIGTERM',
    cwd: undefined,
    env: undefined,
    shell: false,
    ...options,
  };
  validateTimeout(options.timeout);
  validateMaxBuffer(options.maxBuffer);
  convertSignal(options.killSignal);

  const child = spawn(file, args, {
    cwd: options.cwd,
    env: options.env,
    shell: options.shell,
    argv0: options.argv0,
    uid: options.uid,
    gid: options.gid,
    windowsHide: options.windowsHide,
  });

  const encoding = options.encoding && options.encoding !== 'buffer' && Buffer.isEncoding?.(options.encoding) !== false
    ? options.encoding : null;
  const stdoutChunks = [];
  const stderrChunks = [];
  let stdoutLength = 0;
  let stderrLength = 0;
  let killed = false;
  let exited = false;
  let timer = null;
  let ex = null;
  const cmd = [file, ...(Array.isArray(args) ? args : [])].join(' ');

  function output(chunks) {
    const buffer = Buffer.concat(chunks);
    return encoding ? buffer.toString(encoding) : buffer;
  }

  function exitHandler(code, signal) {
    if (exited) return;
    exited = true;
    if (timer) {
      clearTimeout(timer);
      timer = null;
    }
    if (!callback) return;

    const stdout = output(stdoutChunks);
    const stderr = output(stderrChunks);
    if (!ex && code === 0 && signal === null) {
      callback(null, stdout, stderr);
      return;
    }
    const command = options.shell ? file : cmd;
    if (!ex) {
      ex = new Error('Command failed: ' + command + '\n' + stderr);
      ex.code = code < 0 ? native.errname(code) : code;
      ex.killed = child.killed || killed;
      ex.signal = signal;
    }
    ex.cmd = command;
    callback(ex, stdout, stderr);
  }

  function kill() {
    child.stdout?.destroy();
    child.stderr?.destroy();
    killed = true;
    try {
      child.kill(options.killSignal);
    } catch (err) {
      ex = err;
      exitHandler();
    }
  }

  if (options.timeout > 0) {
    timer = setTimeout(() => {
      timer = null;
      kill();
    }, options.timeout);
  }

  function collect(chunks, name) {
    return (chunk) => {
      const length = name === 'stdout' ? (stdoutLength += chunk.length) : (stderrLength += chunk.length);
      if (length > options.maxBuffer) {
        const keep = chunk.length - (length - options.maxBuffer);
        if (keep > 0) chunks.push(chunk.slice(0, keep));
        ex = makeError(RangeError, 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER', `${name} maxBuffer length exceeded`);
        kill();
      } else {
        chunks.push(chunk);
      }
    };
  }
  if (child.stdout) child.stdout.on('data', collect(stdoutChunks, 'stdout'));
  if (child.stderr) child.stderr.on('data', collect(stderrChunks, 'stderr'));

  child.addListener('close', exitHandler);
  child.addListener('error', (err) => {
    ex = err;
    child.stdout?.destroy();
    child.stderr?.destroy();
    exitHandler();
  });
  return child;
}

// util.promisify(exec) and util.promisify(execFile) resolve { stdout, stderr }
const promisifyCustom = Symbol.for('nodejs.util.promisify.custom');
function promisifyExec(fn) {
  return function (...args) {
    let child;
    const promise = new Promise((resolve, reject) => {
      child = fn(...args, (err, stdout, stderr) => {
        if (err) {
          err.stdout = stdout;
          err.stderr = stderr;
          reject(err);
        } else {
          resolve({ stdout, stderr });
        }
      });
    });
    promise.child = child;
    return promise;
  };
}
Object.defineProperty(exec, promisifyCustom, { value: promisifyExec(exec), enumerable: false });
Object.defineProperty(execFile, promisifyCustom, { value: promisifyExec(execFile), enumerable: false });

function inputBytes(input, encoding) {
  if (input === undefined || input === null) return undefined;
  if (typeof input === 'string') return Buffer.from(input, encoding || 'utf8');
  if (Buffer.isBuffer(input) || ArrayBuffer.isView(input)) return Buffer.from(input);
  throw invalidArgType('options.stdio[0]', 'of type string or an instance of Buffer, TypedArray, or DataView', input);
}

// spawnSync(file[, args][, options])
function spawnSync(file, args, options) {
  const opts = normalizeSpawnArguments(file, args, options);
  options = { maxBuffer: 1024 * 1024, ...opts };
  validateTimeout(options.timeout);
  validateMaxBuffer(options.maxBuffer);
  const killSignal = convertSignal(options.killSignal === undefined ? 'SIGTERM' : options.killSignal);
  const stdio = normalizeStdio(options.stdio);

  const result = native.spawnSync({
    ...options,
    stdio,
    input: inputBytes(options.input, options.encoding),
    timeout: options.timeout || 0,
    maxBuffer: options.maxBuffer === Infinity ? -1 : options.maxBuffer,
    killSignal,
  });

  if (result.output) {
    const encoding = options.encoding && options.encoding !== 'buffer' ? options.encoding : null;
    if (encoding) result.output = result.output.map((b) => (b ? b.toString(encoding) : b));
    result.stdout = result.output[1];
    result.stderr = result.output[2];
  } else {
    result.stdout = undefined;
    result.stderr = undefined;
  }
  result.signal = result.signal ? signalNames[result.signal] || result.signal : null;
  if (result.error !== undefined) {
    const err = errnoException(result.error, 'spawnSync ' + opts.file);
    err.path = opts.file;
    err.spawnargs = opts.args.slice(1);
    result.error = err;
  }
  return {
    ...(result.error ? { error: result.error } : {}),
    status: result.status,
    signal: result.signal,
    output: result.output,
    pid: result.pid,
    stdout: result.stdout,
    stderr: result.stderr,
  };
}

// The error execSync/execFileSync throw, or undefined when the child succeeded
function checkExecSyncError(ret, command) {
  let err;
  if (ret.error) {
    err = ret.error;
    Object.assign(err, ret);
    delete err.error;
  } else if (ret.status !== 0) {
    let message = 'Command failed: ' + command;
    if (ret.stderr && ret.stderr.length > 0) message += '\n' + ret.stderr.toString();
    err = new Error(message);
    Object.assign(err, ret);
  }
  return err;
}

// Unless stdio is given, the child's stderr goes to the parent's stderr
function inheritStderr(options) {
  options = { ...options };
  if (options.stdio === undefined) options.stdio = ['pipe', 'pipe', 'inherit'];
  return options;
}

function execFileSync(file, args, options) {
  if (args !== null && typeof args === 'object' && !Array.isArray(args)) {
    options = args;
    args = [];
  }
  options = inheritStderr(options);
  const ret = spawnSync(file, args, options);
  const err = checkExecSyncError(ret, [file, ...(args || [])].join(' '));
  if (err) throw err;
  return ret.stdout;
}

function execSync(command, options) {
  const opts = normalizeExecArgs(command, options, null);
  const ret = spawnSync(opts.file, inheritStderr(opts.options));
  const err = checkExecSyncError(ret, command);
  if (err) throw err;
  return ret.stdout;
}

return {
  ChildProcess,
  spawn,
  exec,
  execFile,
  spawnSync,
  execSync,
  execFileSync,
};
})
)JS";

namespace {

// Object template for process and pipe handles (internal field 0: the wrap)
thread_local Persistent<ObjectTemplate> g_handle_template;

// A running child, owned by its JS handle until it exits
struct ProcessWrap {
    uv_process_t process;
    Isolate* isolate;
    Global<Context> context;
    Global<Object> object;
};

// The parent's end of a stdio pipe
struct PipeWrap {
    uv_pipe_t pipe;
    Isolate* isolate;
    Global<Context> context;
    Global<Object> object;
    bool closing = false;
};

struct WriteReq {
    uv_write_t req;
    std::string data;
    PipeWrap* pipe;
    Global<Function> callback;
};

struct ShutdownReq {
    uv_shutdown_t req;
    PipeWrap* pipe;
    Global<Function> callback;
};

struct SignalName {
    const char* name;
    int number;
};

const SignalName kSignals[] = {
    {"SIGHUP", SIGHUP}, {"SIGINT", SIGINT}, {"SIGQUIT", SIGQUIT}, {"SIGILL", SIGILL},
    {"SIGTRAP", SIGTRAP}, {"SIGABRT", SIGABRT}, {"SIGIOT", SIGIOT}, {"SIGBUS", SIGBUS},
    {"SIGFPE", SIGFPE}, {"SIGKILL", SIGKILL}, {"SIGUSR1", SIGUSR1}, {"SIGSEGV", SIGSEGV},
    {"SIGUSR2", SIGUSR2}, {"SIGPIPE", SIGPIPE}, {"SIGALRM", SIGALRM}, {"SIGTERM", SIGTERM},
    {"SIGCHLD", SIGCHLD}, {"SIGCONT", SIGCONT}, {"SIGSTOP", SIGSTOP}, {"SIGTSTP", SIGTSTP},
    {"SIGTTIN", SIGTTIN}, {"SIGTTOU", SIGTTOU}, {"SIGURG", SIGURG}, {"SIGXCPU", SIGXCPU},
    {"SIGXFSZ", SIGXFSZ}, {"SIGVTALRM", SIGVTALRM}, {"SIGPROF", SIGPROF}, {"SIGWINCH", SIGWINCH},
    {"SIGIO", SIGIO}, {"SIGSYS", SIGSYS},
#ifdef SIGSTKFLT
    {"SIGSTKFLT", SIGSTKFLT},
#endif
#ifdef SIGPWR
    {"SIGPWR", SIGPWR},
#endif
#ifdef SIGINFO
    {"SIGINFO", SIGINFO},
#endif
};

Local<String> V8String(Isolate* isolate, const char* value) {
    return String::NewFromUtf8(isolate, value).ToLocalChecked();
}

std::string ToStdString(Isolate* isolate, Local<Value> value) {
    String::Utf8Value str(isolate, value);
    return *str != nullptr ? std::string(*str, str.length()) : std::string();
}

Local<Value> GetOption(Isolate* isolate, Local<Context> context, Local<Object> options, const char* name) {
    return options->Get(context, V8String(isolate, name)).ToLocalChecked();
}

std::vector<std::string> StringArray(Isolate* isolate, Local<Context> context, Local<Value> value) {
    std::vector<std::string> result;
    if (!value->IsArray()) return result;
    Local<Array> array = value.As<Array>();
    for (uint32_t i = 0; i < array->Length(); i++) {
        result.push_back(ToStdString(isolate, array->Get(context, i).ToLocalChecked()));
    }
    return result;
}

// NULL-terminated char* array pointing into strings
std::vector<char*> CStringArray(std::vector<std::string>& strings) {
    std::vector<char*> result;
    for (std::string& s : strings) result.push_back(&s[0]);
    result.push_back(nullptr);
    return result;
}

template <typename T>
T* Unwrap(Local<Value> value) {
    if (!value->IsObject() || value.As<Object>()->InternalFieldCount() < 1) return nullptr;
    Local<Value> field = value.As<Object>()->GetInternalField(0).As<Value>();
    if (!field->IsExternal()) return nullptr;
    return frogjs::ExternalValue<T>(field);
}

Local<Object> NewHandleObject(Isolate* isolate, Local<Context> context, void* wrap) {
    Local<Object> object = Local<ObjectTemplate>::New(isolate, g_handle_template)->NewInstance(context).ToLocalChecked();
    object->SetInternalField(0, frogjs::NewExternal(isolate, wrap));
    return object;
}

// Call object[name](...argv), reporting errors
void CallMethod(Isolate* isolate, Local<Context> context, Local<Object> object, const char* name,
                int argc, Local<Value> argv[]) {
    TryCatch try_catch(isolate);
    Local<Value> method;
    if (!object->Get(context, V8String(isolate, name)).ToLocal(&method) || !method->IsFunction()) return;
    Local<Value> ignored;
    if (!method.As<Function>()->Call(context, object, argc, argv).ToLocal(&ignored) && try_catch.HasCaught()) {
        frogjs::HandleUncaught(isolate, &try_catch);
    }
}

void CallFunction(Isolate* isolate, Local<Context> context, Local<Function> function, int argc, Local<Value> argv[]) {
    TryCatch try_catch(isolate);
    Local<Value> ignored;
    if (!function->Call(context, Undefined(isolate), argc, argv).ToLocal(&ignored) && try_catch.HasCaught()) {
        frogjs::HandleUncaught(isolate, &try_catch);
    }
}

// Bytes of a Buffer / TypedArray / string argument
std::string BytesArg(Isolate* isolate, Local<Value> value) {
    std::string bytes;
    if (value->IsString()) return frogjs::DecodeString(isolate, value.As<String>(), frogjs::Encoding::kUtf8);
    frogjs::GetBinaryData(value, &bytes);
    return bytes;
}

// Fill process options shared by spawn() and spawnSync(). The strings must
// outlive the uv_spawn() call.
struct SpawnStrings {
    std::string file;
    std::string cwd;
    std::vector<std::string> args;
    std::vector<std::string> env;
    std::vector<char*> argv;
    std::vector<char*> envp;
};

void FillProcessOptions(Isolate* isolate, Local<Context> context, Local<Object> options,
                        SpawnStrings* strings, uv_process_options_t* opts) {
    strings->file = ToStdString(isolate, GetOption(isolate, context, options, "file"));
    strings->args = StringArray(isolate, context, GetOption(isolate, context, options, "args"));
    strings->argv = CStringArray(strings->args);
    opts->file = strings->file.c_str();
    opts->args = strings->argv.data();

    Local<Value> cwd = GetOption(isolate, context, options, "cwd");
    if (cwd->IsString()) {
        strings->cwd = ToStdString(isolate, cwd);
        opts->cwd = strings->cwd.c_str();
    }
    Local<Value> env = GetOption(isolate, context, options, "envPairs");
    if (env->IsArray()) {
        strings->env = StringArray(isolate, context, env);
        strings->envp = CStringArray(strings->env);
        opts->env = strings->envp.data();
    }
    if (GetOption(isolate, context, options, "detached")->IsTrue()) opts->flags |= UV_PROCESS_DETACHED;
    Local<Value> uid = GetOption(isolate, context, options, "uid");
    if (uid->IsNumber()) {
        opts->flags |= UV_PROCESS_SETUID;
        opts->uid = static_cast<uv_uid_t>(uid->Int32Value(context).FromJust());
    }
    Local<Value> gid = GetOption(isolate, context, options, "gid");
    if (gid->IsNumber()) {
        opts->flags |= UV_PROCESS_SETGID;
        opts->gid = static_cast<uv_gid_t>(gid->Int32Value(context).FromJust());
    }
}

// stdio entries: { type: 'pipe' | 'ignore' | 'fd', fd }
struct StdioEntry {
    std::string type;
    int fd = -1;
};

std::vector<StdioEntry> StdioEntries(Isolate* isolate, Local<Context> context, Local<Object> options) {
    std::vector<StdioEntry> entries;
    Local<Value> stdio = GetOption(isolate, context, options, "stdio");
    if (!stdio->IsArray()) return entries;
    Local<Array> array = stdio.As<Array>();
    for (uint32_t i = 0; i < array->Length(); i++) {
        Local<Value> item = array->Get(context, i).ToLocalChecked();
        StdioEntry entry;
        if (item->IsObject()) {
            Local<Object> object = item.As<Object>();
            entry.type = ToStdString(isolate, GetOption(isolate, context, object, "type"));
            Local<Value> fd = GetOption(isolate, context, object, "fd");
            if (fd->IsNumber()) entry.fd = fd->Int32Value(context).FromJust();
        }
        entries.push_back(entry);
    }
    return entries;
}

// ---- spawn() --------------------------------------------------------------

void OnProcessExit(uv_process_t* process, int64_t exit_status, int term_signal) {
    ProcessWrap* wrap = static_cast<ProcessWrap*>(process->data);
    Isolate* isolate = wrap->isolate;
    HandleScope handle_scope(isolate);
    Local<Context> context = wrap->context.Get(isolate);
    Context::Scope context_scope(context);

    Local<Object> object = wrap->object.Get(isolate);
    object->SetInternalField(0, Undefined(isolate));
    uv_close(reinterpret_cast<uv_handle_t*>(process), [](uv_handle_t* handle) {
        delete static_cast<ProcessWrap*>(handle->data);
    });

    Local<Value> argv[2] = {Number::New(isolate, static_cast<double>(exit_status)), Integer::New(isolate, term_signal)};
    CallMethod(isolate, context, object, "onexit", 2, argv);
}

void ClosePipeWrap(PipeWrap* wrap) {
    if (wrap->closing) return;
    wrap->closing = true;
    uv_close(reinterpret_cast<uv_handle_t*>(&wrap->pipe), [](uv_handle_t* handle) {
        PipeWrap* wrap = static_cast<PipeWrap*>(handle->data);
        Isolate* isolate = wrap->isolate;
        {
            HandleScope handle_scope(isolate);
            Local<Context> context = wrap->context.Get(isolate);
            Context::Scope context_scope(context);
            Local<Object> object = wrap->object.Get(isolate);
            object->SetInternalField(0, Undefined(isolate));
            CallMethod(isolate, context, object, "onclose", 0, nullptr);
        }
        delete wrap;
    });
}

// spawn(options) -> handle with pid and stdio (pipe handles), or an error code
void Spawn(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    if (!args[0]->IsObject()) return;
    Local<Object> options = args[0].As<Object>();

    uv_process_options_t opts;
    memset(&opts, 0, sizeof(opts));
    SpawnStrings strings;
    FillProcessOptions(isolate, context, options, &strings, &opts);
    opts.exit_cb = OnProcessExit;

    std::vector<StdioEntry> entries = StdioEntries(isolate, context, options);
    std::vector<uv_stdio_container_t> containers(entries.size());
    std::vector<PipeWrap*> pipes(entries.size(), nullptr);
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].type == "pipe") {
            PipeWrap* pipe = new PipeWrap;
            pipe->isolate = isolate;
            pipe->context.Reset(isolate, context);
            uv_pipe_init(frogjs::Loop(), &pipe->pipe, 0);
            pipe->pipe.data = pipe;
            pipes[i] = pipe;
            containers[i].flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE);
            containers[i].data.stream = reinterpret_cast<uv_stream_t*>(&pipe->pipe);
        } else if (entries[i].type == "fd") {
            containers[i].flags = UV_INHERIT_FD;
            containers[i].data.fd = entries[i].fd;
        } else {
            containers[i].flags = UV_IGNORE;
        }
    }
    opts.stdio = containers.data();
    opts.stdio_count = static_cast<int>(containers.size());

    ProcessWrap* wrap = new ProcessWrap;
    wrap->isolate = isolate;
    wrap->context.Reset(isolate, context);
    wrap->process.data = wrap;
    int err = uv_spawn(frogjs::Loop(), &wrap->process, &opts);
    if (err != 0) {
        // The handle is initialized even when spawning fails
        uv_close(reinterpret_cast<uv_handle_t*>(&wrap->process), [](uv_handle_t* handle) {
            delete static_cast<ProcessWrap*>(handle->data);
        });
        for (PipeWrap* pipe : pipes) {
            if (pipe == nullptr) continue;
            uv_close(reinterpret_cast<uv_handle_t*>(&pipe->pipe), [](uv_handle_t* handle) {
                delete static_cast<PipeWrap*>(handle->data);
            });
        }
        args.GetReturnValue().Set(err);
        return;
    }

    Local<Object> object = NewHandleObject(isolate, context, wrap);
    wrap->object.Reset(isolate, object);
    object->Set(context, V8String(isolate, "pid"), Integer::New(isolate, wrap->process.pid)).Check();

    Local<Array> stdio = Array::New(isolate, static_cast<int>(pipes.size()));
    for (size_t i = 0; i < pipes.size(); i++) {
        Local<Value> value = Null(isolate);
        if (pipes[i] != nullptr) {
            Local<Object> pipe_object = NewHandleObject(isolate, context, pipes[i]);
            pipes[i]->object.Reset(isolate, pipe_object);
            value = pipe_object;
        }
        stdio->Set(context, static_cast<uint32_t>(i), value).Check();
    }
    object->Set(context, V8String(isolate, "stdio"), stdio).Check();
    args.GetReturnValue().Set(object);
}

void Kill(const FunctionCallbackInfo<Value>& args) {
    ProcessWrap* wrap = Unwrap<ProcessWrap>(args[0]);
    if (wrap == nullptr) {
        args.GetReturnValue().Set(UV_ESRCH);
        return;
    }
    int signum = args[1]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(SIGTERM);
    args.GetReturnValue().Set(uv_process_kill(&wrap->process, signum));
}

void Ref(const FunctionCallbackInfo<Value>& args) {
    ProcessWrap* wrap = Unwrap<ProcessWrap>(args[0]);
    if (wrap == nullptr) return;
    uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&wrap->process);
    if (args[1]->IsTrue()) uv_ref(handle);
    else uv_unref(handle);
}

void OnPipeAlloc(uv_handle_t*, size_t suggested_size, uv_buf_t* buf) {
    buf->base = new char[suggested_size];
    buf->len = suggested_size;
}

void OnPipeRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    PipeWrap* wrap = static_cast<PipeWrap*>(stream->data);
    std::unique_ptr<char[]> data(buf->base);
    if (nread == 0 || wrap->closing) return;

    Isolate* isolate = wrap->isolate;
    HandleScope handle_scope(isolate);
    Local<Context> context = wrap->context.Get(isolate);
    Context::Scope context_scope(context);

    Local<Value> argv[1];
    if (nread > 0) {
        argv[0] = NewBuffer(isolate, context, buf->base, static_cast<size_t>(nread));
    } else {
        uv_read_stop(stream);
        argv[0] = nread == UV_EOF ? Null(isolate).As<Value>() : Integer::New(isolate, static_cast<int>(nread)).As<Value>();
    }
    CallMethod(isolate, context, wrap->object.Get(isolate), "onread", 1, argv);
}

void ReadStart(const FunctionCallbackInfo<Value>& args) {
    PipeWrap* wrap = Unwrap<PipeWrap>(args[0]);
    if (wrap == nullptr || wrap->closing) return;
    uv_read_start(reinterpret_cast<uv_stream_t*>(&wrap->pipe), OnPipeAlloc, OnPipeRead);
}

void ReadStop(const FunctionCallbackInfo<Value>& args) {
    PipeWrap* wrap = Unwrap<PipeWrap>(args[0]);
    if (wrap == nullptr || wrap->closing) return;
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&wrap->pipe));
}

// write(pipe, data, callback(status)) -> 0 or an error code
void Write(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    PipeWrap* wrap = Unwrap<PipeWrap>(args[0]);
    if (wrap == nullptr || wrap->closing) {
        args.GetReturnValue().Set(UV_EPIPE);
        return;
    }
    WriteReq* req = new WriteReq;
    req->data = BytesArg(isolate, args[1]);
    req->pipe = wrap;
    if (args[2]->IsFunction()) req->callback.Reset(isolate, args[2].As<Function>());
    req->req.data = req;
    uv_buf_t buf = uv_buf_init(req->data.empty() ? nullptr : &req->data[0], static_cast<unsigned int>(req->data.size()));
    int err = uv_write(&req->req, reinterpret_cast<uv_stream_t*>(&wrap->pipe), &buf, 1, [](uv_write_t* uv_req, int status) {
        WriteReq* req = static_cast<WriteReq*>(uv_req->data);
        Isolate* isolate = req->pipe->isolate;
        if (!req->callback.IsEmpty() && status != UV_ECANCELED) {
            HandleScope handle_scope(isolate);
            Local<Context> context = req->pipe->context.Get(isolate);
            Context::Scope context_scope(context);
            Local<Value> argv[1] = {Integer::New(isolate, status)};
            CallFunction(isolate, context, req->callback.Get(isolate), 1, argv);
        }
        delete req;
    });
    if (err != 0) delete req;
    args.GetReturnValue().Set(err);
}

// shutdown(pipe, callback(status)): end the child's input once writes are done
void Shutdown(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    PipeWrap* wrap = Unwrap<PipeWrap>(args[0]);
    if (wrap == nullptr || wrap->closing) return;
    ShutdownReq* req = new ShutdownReq;
    req->pipe = wrap;
    if (args[1]->IsFunction()) req->callback.Reset(isolate, args[1].As<Function>());
    req->req.data = req;
    int err = uv_shutdown(&req->req, reinterpret_cast<uv_stream_t*>(&wrap->pipe), [](uv_shutdown_t* uv_req, int status) {
        ShutdownReq* req = static_cast<ShutdownReq*>(uv_req->data);
        Isolate* isolate = req->pipe->isolate;
        if (!req->callback.IsEmpty() && status != UV_ECANCELED) {
            HandleScope handle_scope(isolate);
            Local<Context> context = req->pipe->context.Get(isolate);
            Context::Scope context_scope(context);
            Local<Value> argv[1] = {Integer::New(isolate, status)};
            CallFunction(isolate, context, req->callback.Get(isolate), 1, argv);
        }
        delete req;
    });
    if (err != 0) {
        delete req;
        if (args[1]->IsFunction()) {
            Local<Value> argv[1] = {Integer::New(isolate, err)};
            CallFunction(isolate, isolate->GetCurrentContext(), args[1].As<Function>(), 1, argv);
        }
    }
}

void ClosePipe(const FunctionCallbackInfo<Value>& args) {
    PipeWrap* wrap = Unwrap<PipeWrap>(args[0]);
    if (wrap != nullptr) ClosePipeWrap(wrap);
}

// ---- spawnSync() ----------------------------------------------------------

struct SyncRunner;

struct SyncPipe {
    uv_pipe_t pipe;
    SyncRunner* runner;
    std::string output;
    bool used = false;
};

struct SyncRunner {
    uv_loop_t loop;
    uv_process_t process;
    uv_timer_t timer;
    SyncPipe pipes[3];
    std::string input;
    uv_write_t write_req;
    uv_shutdown_t shutdown_req;
    int64_t exit_status = 0;
    int term_signal = 0;
    bool exited = false;
    bool timer_used = false;
    int error = 0;
    int64_t max_buffer = -1;  // -1: unlimited
    int kill_signal = SIGTERM;
};

void SyncKill(SyncRunner* runner, int error) {
    if (runner->error == 0) runner->error = error;
    if (!runner->exited) uv_process_kill(&runner->process, runner->kill_signal);
}

void CloseIfOpen(uv_handle_t* handle) {
    if (!uv_is_closing(handle)) uv_close(handle, nullptr);
}

void OnSyncRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    SyncPipe* pipe = static_cast<SyncPipe*>(stream->data);
    std::unique_ptr<char[]> data(buf->base);
    if (nread > 0) {
        SyncRunner* runner = pipe->runner;
        // Like Node.js the read that goes past maxBuffer is kept whole
        pipe->output.append(buf->base, static_cast<size_t>(nread));
        if (runner->max_buffer >= 0 && pipe->output.size() > static_cast<size_t>(runner->max_buffer)) {
            SyncKill(runner, UV_ENOBUFS);
            uv_read_stop(stream);
            CloseIfOpen(reinterpret_cast<uv_handle_t*>(stream));
        }
    } else if (nread < 0) {
        CloseIfOpen(reinterpret_cast<uv_handle_t*>(stream));
    }
}

// spawnSync(options) -> { pid, status, signal, output, error }
void SpawnSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    if (!args[0]->IsObject()) return;
    Local<Object> options = args[0].As<Object>();

    auto runner = std::make_unique<SyncRunner>();
    uv_loop_init(&runner->loop);

    uv_process_options_t opts;
    memset(&opts, 0, sizeof(opts));
    SpawnStrings strings;
    FillProcessOptions(isolate, context, options, &strings, &opts);
    opts.exit_cb = [](uv_process_t* process, int64_t exit_status, int term_signal) {
        SyncRunner* runner = static_cast<SyncRunner*>(process->data);
        runner->exited = true;
        runner->exit_status = exit_status;
        runner->term_signal = term_signal;
        CloseIfOpen(reinterpret_cast<uv_handle_t*>(process));
        if (runner->timer_used) CloseIfOpen(reinterpret_cast<uv_handle_t*>(&runner->timer));
    };

    Local<Value> input = GetOption(isolate, context, options, "input");
    if (!input->IsUndefined() && !input->IsNull()) runner->input = BytesArg(isolate, input);
    Local<Value> max_buffer = GetOption(isolate, context, options, "maxBuffer");
    if (max_buffer->IsNumber()) runner->max_buffer = static_cast<int64_t>(max_buffer->NumberValue(context).FromJust());
    Local<Value> kill_signal = GetOption(isolate, context, options, "killSignal");
    if (kill_signal->IsNumber()) runner->kill_signal = kill_signal->Int32Value(context).FromJust();
    int64_t timeout = 0;
    Local<Value> timeout_value = GetOption(isolate, context, options, "timeout");
    if (timeout_value->IsNumber()) timeout = static_cast<int64_t>(timeout_value->NumberValue(context).FromJust());

    std::vector<StdioEntry> entries = StdioEntries(isolate, context, options);
    std::vector<uv_stdio_container_t> containers(entries.size());
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].type == "pipe" && i < 3) {
            SyncPipe* pipe = &runner->pipes[i];
            pipe->runner = runner.get();
            pipe->used = true;
            uv_pipe_init(&runner->loop, &pipe->pipe, 0);
            pipe->pipe.data = pipe;
            containers[i].flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE);
            containers[i].data.stream = reinterpret_cast<uv_stream_t*>(&pipe->pipe);
        } else if (entries[i].type == "fd") {
            containers[i].flags = UV_INHERIT_FD;
            containers[i].data.fd = entries[i].fd;
        } else {
            containers[i].flags = UV_IGNORE;
        }
    }
    opts.stdio = containers.data();
    opts.stdio_count = static_cast<int>(containers.size());

    runner->process.data = runner.get();
    int err = uv_spawn(&runner->loop, &runner->process, &opts);
    if (err != 0) {
        runner->error = err;
        CloseIfOpen(reinterpret_cast<uv_handle_t*>(&runner->process));
        for (SyncPipe& pipe : runner->pipes) {
            if (pipe.used) CloseIfOpen(reinterpret_cast<uv_handle_t*>(&pipe.pipe));
        }
    } else {
        for (int i = 0; i < 3; i++) {
            SyncPipe* pipe = &runner->pipes[i];
            if (!pipe->used) continue;
            uv_stream_t* stream = reinterpret_cast<uv_stream_t*>(&pipe->pipe);
            if (i == 0) {
                // Write the input (if any), then end the child's stdin
                if (!runner->input.empty()) {
                    uv_buf_t buf = uv_buf_init(&runner->input[0], static_cast<unsigned int>(runner->input.size()));
                    uv_write(&runner->write_req, stream, &buf, 1, nullptr);
                }
                runner->shutdown_req.data = pipe;
                if (uv_shutdown(&runner->shutdown_req, stream, [](uv_shutdown_t* req, int) {
                        CloseIfOpen(reinterpret_cast<uv_handle_t*>(req->handle));
                    }) != 0) {
                    CloseIfOpen(reinterpret_cast<uv_handle_t*>(stream));
                }
            } else {
                uv_read_start(stream, OnPipeAlloc, OnSyncRead);
            }
        }
        if (timeout > 0) {
            runner->timer_used = true;
            uv_timer_init(&runner->loop, &runner->timer);
            runner->timer.data = runner.get();
            uv_timer_start(&runner->timer, [](uv_timer_t* timer) {
                SyncKill(static_cast<SyncRunner*>(timer->data), UV_ETIMEDOUT);
            }, static_cast<uint64_t>(timeout), 0);
        }
    }
    uv_run(&runner->loop, UV_RUN_DEFAULT);
    uv_loop_close(&runner->loop);

    Local<Object> result = Object::New(isolate);
    bool spawned = err == 0;
    result->Set(context, V8String(isolate, "pid"), Integer::New(isolate, spawned ? runner->process.pid : 0)).Check();
    Local<Value> status = Null(isolate);
    if (spawned && runner->term_signal == 0) status = Number::New(isolate, static_cast<double>(runner->exit_status));
    result->Set(context, V8String(isolate, "status"), status).Check();
    result->Set(context, V8String(isolate, "signal"),
        spawned && runner->term_signal != 0 ? Integer::New(isolate, runner->term_signal).As<Value>() : Null(isolate).As<Value>()).Check();

    Local<Value> output = Null(isolate);
    if (spawned) {
        Local<Array> array = Array::New(isolate, static_cast<int>(entries.size()));
        for (size_t i = 0; i < entries.size(); i++) {
            Local<Value> value = Null(isolate);
            if (i > 0 && i < 3 && runner->pipes[i].used) {
                value = NewBuffer(isolate, context, runner->pipes[i].output.data(), runner->pipes[i].output.size());
            }
            array->Set(context, static_cast<uint32_t>(i), value).Check();
        }
        output = array;
    }
    result->Set(context, V8String(isolate, "output"), output).Check();
    if (runner->error != 0) result->Set(context, V8String(isolate, "error"), Integer::New(isolate, runner->error)).Check();
    args.GetReturnValue().Set(result);
}

void ErrName(const FunctionCallbackInfo<Value>& args) {
    int err = args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
    args.GetReturnValue().Set(V8String(args.GetIsolate(), uv_err_name(err)));
}

}  // namespace

// Set up child_process module (requires events, stream and Buffer), exposed as
// global `child_process` and returned by require('child_process')
void SetupChildProcess(Isolate* isolate, Local<Context> context) {
    Local<Object> global = context->Global();
    Local<ObjectTemplate> handle_template = ObjectTemplate::New(isolate);
    handle_template->SetInternalFieldCount(1);
    g_handle_template.Reset(isolate, handle_template);

    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "spawn", Spawn);
    frogjs::SetFunction(isolate, context, native, "spawnSync", SpawnSync);
    frogjs::SetFunction(isolate, context, native, "kill", Kill);
    frogjs::SetFunction(isolate, context, native, "ref", Ref);
    frogjs::SetFunction(isolate, context, native, "readStart", ReadStart);
    frogjs::SetFunction(isolate, context, native, "readStop", ReadStop);
    frogjs::SetFunction(isolate, context, native, "write", Write);
    frogjs::SetFunction(isolate, context, native, "shutdown", Shutdown);
    frogjs::SetFunction(isolate, context, native, "closePipe", ClosePipe);
    frogjs::SetFunction(isolate, context, native, "errname", ErrName);

    Local<Object> signals = Object::New(isolate);
    for (const SignalName& signal : kSignals) {
        signals->Set(context, V8String(isolate, signal.name), Integer::New(isolate, signal.number)).Check();
    }
    native->Set(context, V8String(isolate, "signals"), signals).Check();
    Local<Object> errno_values = Object::New(isolate);
    errno_values->Set(context, V8String(isolate, "ESRCH"), Integer::New(isolate, UV_ESRCH)).Check();
    native->Set(context, V8String(isolate, "errno"), errno_values).Check();

    const char* names[] = {"events", "stream", "Buffer", "path"};
    Local<Value> argv[5] = {native};
    for (int i = 0; i < 4; i++) {
        argv[i + 1] = global->Get(context, V8String(isolate, names[i])).ToLocalChecked();
    }
    Local<Value> child_process;
    if (!frogjs::RunJsModule(isolate, context, "child_process", kChildProcessSource, 5, argv).ToLocal(&child_process)) {
        return;
    }
    global->Set(context, V8String(isolate, "child_process"), child_process).Check();
}
