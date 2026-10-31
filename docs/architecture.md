# FrogJS Architecture

FrogJS is a JavaScript runtime in the mould of Node.js: **V8** runs the JavaScript, **libuv** provides the event loop, I/O and threads, and about 14,000 lines of C++ (half of it JavaScript embedded in C++ files) connect the two and implement the built-in modules. It has no other dependencies: hashing, HTTP parsing and `util.inspect` are all part of the runtime.

This document explains how the pieces fit together. For conventions see the [style guide](style-guide.md); for the API see the [API reference](api-reference.md).

## Contents
- [Overview](#overview)
- [Source layout](#source-layout)
- [Startup](#startup)
- [Built-in modules](#built-in-modules)
- [The event loop](#the-event-loop)
- [Module loading](#module-loading)
- [Errors](#errors)
- [Threads](#threads)
- [The REPL](#the-repl)
- [Memory management](#memory-management)
- [Build, tests and tooling](#build-tests-and-tooling)
- [Known limitations](#known-limitations)

## Overview

```
┌────────────────────────────────────────────────────────────────────┐
│ User code (script, --eval, REPL)            types/*.d.ts describe  │
│ require('fs' | './lib/x')                   what it can call       │
├────────────────────────────────────────────────────────────────────┤
│ JavaScript layer (embedded in the C++ files)                       │
│   events · stream · http · util · crypto · child_process ·         │
│   worker_threads · process.stdout                                  │
├────────────────────────────────────────────────────────────────────┤
│ Native bindings (C++)                                              │
│   console · process · Buffer · timers · fs · net · path · os ·     │
│   require() · native helpers of the JS modules (hashing, pipes,    │
│   processes, message ports, ...)                                   │
├──────────────────────────────┬─────────────────────────────────────┤
│ V8: isolates, contexts,      │ libuv: event loop, timers, TCP,     │
│ handles, ValueSerializer     │ pipes, processes, fs and thread     │
│                              │ pool, threads, async wakeups        │
└──────────────────────────────┴─────────────────────────────────────┘
```

Every JavaScript thread (the main thread and each `Worker`) has one **V8 isolate** (its own heap), one **context** (its own global object and copy of every built-in module) and one **libuv loop**.

## Source layout

```
src/core/
  runtime.cpp        main(): options, V8 setup, CreateContext(), ExecuteScript(), the event loop, console
  repl.cpp           the REPL, on top of the event loop
  line_editor.cpp    line editing, history and completion for the REPL
src/bindings/        one file per module, each with a Setup<Name>() function
  process.cpp  buffer.cpp  timers.cpp  fs.cpp  net.cpp  path.cpp  os.cpp  modules.cpp (require)
  events.cpp  stream.cpp  http.cpp  util.cpp                 (mostly embedded JavaScript)
  crypto.cpp  child_process.cpp  worker_threads.cpp           (embedded JavaScript + native helpers)
src/utils/
  js_module.cpp      RunJsModule(): runs an embedded module
  errors.cpp         ReportException(): Node.js-style uncaught error reports
  encoding.cpp       hex / base64 / latin1 / ... shared by Buffer and crypto
  hash.cpp           MD5, SHA-1, SHA-2, HMAC, PBKDF2
include/frogjs/      headers shared between files (all in namespace frogjs)
  runtime.h  loop.h  worker.h  errors.h  js_module.h  util.h  buffer.h  encoding.h  hash.h
  repl.h  line_editor.h  v8_compat.h  version.h
types/               TypeScript declarations of the API (see types/README.md)
examples/            examples and the self-tests (test-*.js)
tools/               development scripts (check-types.js)
```

## Startup

```
main()
  ├── ParseArgs()             --help / --version exit here; bad options exit with 9
  ├── pick the source         --eval code │ script file │ stdin │ nothing → REPL
  ├── V8::InitializePlatform(), V8::Initialize(), Isolate::New()
  ├── CreateContext()         a context with every built-in installed (below)
  ├── ExecuteScript()         compile + run; an uncaught error is reported → exit code 1
  ├── RunEventLoop()          uv_run() until no handle keeps the loop alive (or StartREPL())
  ├── TerminateAllWorkers()   stop and join workers that are still running (unref'd)
  └── dispose the isolate, V8 and the loop
```

`frogjs::CreateContext()` (`include/frogjs/runtime.h`) installs the built-ins in dependency order: each later one can use the earlier ones through the global object.

```
console → util → process → Buffer → timers → fs → net → events → stream (+ process.stdout/stderr)
        → http → crypto → path → os → child_process → worker_threads → require()/__filename/module
```

The REPL's `.clear` and every worker thread call the same function, so all contexts are built the same way.

## Built-in modules

Each `src/bindings/<name>.cpp` exposes `void Setup<Name>(Isolate*, Local<Context>)`, which puts the module on the global object (`fs`, `http`, ...). `require('<name>')` returns that same object; the list of names is in `src/bindings/modules.cpp`.

There are two kinds:

**Native modules** (`fs`, `net`, `path`, `os`, `timers`, `Buffer`, `process`) are C++ functions set as properties of an object. For example `fs.readFile()` starts a `uv_fs_*` request, and its completion callback calls the JavaScript callback.

**Embedded JavaScript modules** (`events`, `stream`, `http`, `util`, `crypto`, `child_process`, `worker_threads`) are JavaScript in a C++ raw string. The source is one function expression; `frogjs::RunJsModule()` (`src/utils/js_module.cpp`) compiles it as `frogjs:<name>` (the name in stack traces) and calls it with the module's dependencies. Its return value is the module.

```cpp
static const char* kCryptoSource = R"JS(
(function (native, stream) {
'use strict';
class Hash extends stream.Transform { /* ... uses native.digestUpdate() ... */ }
return { createHash, Hash, /* ... */ };
})
)JS";

void SetupCrypto(Isolate* isolate, Local<Context> context) {
    Local<Object> native = Object::New(isolate);                 // native helpers
    frogjs::SetFunction(isolate, context, native, "digestUpdate", DigestUpdate);
    Local<Value> argv[2] = {native, /* global.stream */};
    Local<Value> crypto;
    if (!frogjs::RunJsModule(isolate, context, "crypto", kCryptoSource, 2, argv).ToLocal(&crypto)) return;
    context->Global()->Set(context, String::NewFromUtf8Literal(isolate, "crypto"), crypto).Check();
}
```

Logic that is natural in JavaScript (argument checks, Node.js-compatible errors, streams and events) lives in the embedded source. Only what JavaScript can't do is native: system calls, libuv handles, hashing, serialization. Native objects such as a digest, a pipe or a process are given to JavaScript as **handle objects**: an `ObjectTemplate` instance with one internal field pointing at the C++ struct. The JavaScript passes the handle back to native functions (`native.digestUpdate(handle, data)`), and the native side calls methods on it to report events (`handle.onread(buffer)`, `handle.onexit(code, signal)`).

| Module | Native part | JavaScript part |
|--------|-------------|-----------------|
| `events` | none | `EventEmitter` |
| `stream` | byte-string helpers | `Readable`, `Writable`, `Duplex`, `Transform`, `pipeline`, ... |
| `http` | none; it uses `net` | HTTP/1.1 parser and serializer, `Server`, `IncomingMessage`, `ClientRequest`, ... |
| `util` | Proxy/Promise/boxed-value inspection | `inspect`, `format`, `promisify` |
| `crypto` | `hash.cpp` digests, `uv_random`, PBKDF2 on the thread pool | `Hash`/`Hmac` streams, validation |
| `child_process` | `uv_spawn`, pipes, a private loop for `spawnSync` | `ChildProcess`, stdio streams, `exec`, `execSync`, ... |
| `worker_threads` | threads, isolates, message ports, serialization | `Worker`, `MessagePort` methods, `MessageChannel` |

C++ code calls back into embedded JavaScript too: `console.log` formats with `util.format` through `frogjs::Format()`, and error reports use `util.inspect` through `frogjs::Inspect()` (`include/frogjs/util.h`).

## The event loop

`frogjs::RunEventLoop()` runs the thread's libuv loop until no active, referenced handle or request remains:

```cpp
while (uv_run(frogjs::Loop(), UV_RUN_DEFAULT)) {
    isolate->PerformMicrotaskCheckpoint();
}
```

Promise jobs (microtasks) run automatically when a callback into JavaScript returns, so `.then()` callbacks run between libuv callbacks. FrogJS has no `process.nextTick`: the embedded modules defer work with `Promise.resolve().then(...)`.

Every asynchronous operation follows the same path:

```
JavaScript call → binding starts a libuv request or handle, keeps the callback in a Persistent → returns
                                     ⋮
libuv completion on the loop thread → C++ callback: HandleScope, Context::Scope, TryCatch
                                     → call the JavaScript callback → ReportException() if it threw
```

A handle that keeps the process alive is `uv_ref`'d; `unref()` (timers, child processes, workers, message ports) makes it stop counting. Work that would block runs on the libuv thread pool: `fs` requests, `pbkdf2`, async `randomBytes`.

## Module loading

`require()` (`src/bindings/modules.cpp`):

```
require(id)
  ├── a built-in name? → the global of that name
  ├── resolve from the requiring module's directory (the main script's for the main script):
  │     exact path → path.js → path/index.js
  ├── in the cache? → cached exports
  ├── wrap: (function (exports, require, module, __filename, __dirname) { ... })
  │     (compiled with a line offset of -1, so error lines match the file)
  ├── run it with a require() bound to the module's own directory
  └── cache module.exports (any value) and return it
```

The main script is not wrapped: it runs as a plain script, with globals `__filename` and `__dirname` (absolute paths) and `module`/`exports`. That is why TypeScript's CommonJS output runs as the entry point. A module is cached only after it finishes running, so circular `require()`s load the module again instead of returning its partial exports. Packages (`node_modules`, `package.json`) and ES modules are not supported.

## Errors

`frogjs::ReportException()` (`src/utils/errors.cpp`) prints an uncaught error the way Node.js does: `file:line`, the source line with a caret, then the value through `util.inspect` (the stack plus properties such as `code`). It is used for:
- the main script: the report is printed and the process exits with code 1 without running the event loop;
- callbacks from the event loop (timers, fs, sockets, streams): the report is printed and the process keeps running;
- worker threads: the error is sent to the parent instead (see below);
- termination (`process.exit()` in a worker, `worker.terminate()`, Ctrl+C in the REPL): ignored, since it isn't an error.

Errors thrown to JavaScript match Node.js's: the same class, `code` and message (`TypeError [ERR_INVALID_ARG_TYPE]: The "file" argument must be ...`). The self-tests check this by running under both runtimes.

## Threads

### Per-thread state

V8 isolates and libuv loops are single-threaded, so every JavaScript thread has its own. Code finds the current thread's loop with `frogjs::Loop()` (`include/frogjs/loop.h`), a `thread_local` pointer that is null on the main thread (meaning `uv_default_loop()`). State tied to an isolate is `thread_local` too: cached templates and functions, timer IDs, the module cache. The few objects shared between threads (worker state, message queues) are guarded by mutexes. The only cross-thread signals are `uv_async_send()` (wake another loop) and `Isolate::TerminateExecution()`, both thread-safe.

```
 main thread                                   worker thread
┌───────────────────────────┐                ┌───────────────────────────┐
│ isolate A, context, loop  │                │ isolate B, context, loop  │
│                           │   Message      │                           │
│ Worker ── port ═══════════╪═══ queues ═════╪═══ parentPort             │
│   │                       │  (mutex +      │                           │
│   └─ async ◄──────────────╪─ uv_async_send)│  online / error / exit    │
└───────────────────────────┘                └───────────────────────────┘
          shared: SharedArrayBuffer backing stores (std::shared_ptr)
```

### Workers

`new Worker()` (`src/bindings/worker_threads.cpp`) creates a `WorkerThread` (state shared with the new thread) and a parent-side `WorkerHandle`, then starts a thread with a 4 MB stack. The thread:
1. creates its loop and isolate, calls `CreateContext()` (its `worker_threads` module sees `isMainThread: false` and gets `parentPort` and the deserialized `workerData`);
2. marks itself online; from here on `terminate()` can interrupt it;
3. runs the script with `ExecuteScript()`, then its loop until it has nothing left to do or is stopped;
4. closes its ports and handles, disposes the isolate and signals the parent, which joins the thread and emits `'exit'`.

Stopping goes through `StopWorker()`: it records the exit code (the first reason wins), calls `TerminateExecution()` and wakes the worker's loop to stop it. It is used by `terminate()` (code 1), `process.exit(code)` in the worker (`frogjs::ExitWorker()`), an uncaught error (code 1, see `frogjs::ForwardWorkerError()`), and the parent's exit. An uncaught error in a worker is serialized with its extra properties and emitted as the `Worker`'s `'error'` event. Messages the worker posted before ending are delivered before `'exit'`.

### Messages

A channel is two `PortEndpoint`s, each with an incoming queue. `postMessage()` serializes the value with `v8::ValueSerializer` into a `Message`: the bytes, plus the backing stores of `SharedArrayBuffer`s (shared, not copied) and of transferred `ArrayBuffer`s (detached from the sender), plus transferred endpoints. It appends the message to the peer's queue and wakes the peer's thread. That thread deserializes the message and emits `'message'`. An endpoint being transferred has no owning thread; messages wait in its queue until it is attached on the receiving side. Buffers (FrogJS host objects) are serialized by the delegate and arrive as `Uint8Array`s.

### Other threads

The libuv thread pool runs `fs` requests, `pbkdf2` and `randomBytes`, without touching V8. The REPL's Ctrl+C watchdog thread only calls `TerminateExecution()`. `spawnSync()` runs a child on a private loop on the calling thread.

## The REPL

The REPL (`src/core/repl.cpp`) doesn't block on input: stdin is watched with a `uv_poll_t`, so timers and sockets keep running while the prompt waits.

```
StartREPL()
  ├── uv_poll on stdin ──► bytes ──► LineEditor::Feed()
  │                                   ├── kLine      → evaluate, print, new prompt
  │                                   ├── kInterrupt → clear input / warn / exit
  │                                   └── kEof       → exit
  └── uv_run() until the REPL closes
```

- The **LineEditor** puts the terminal in raw mode while the prompt is shown and restores it while code runs. It handles editing keys, history and completion, and redraws the prompt around asynchronous output (`ReplBeforeOutput()`/`ReplAfterOutput()` around console writes on the main thread).
- Each input is compiled as `REPL<n>`. A `SyntaxError` at the end of the input keeps it buffered for the next line.
- Ctrl+C while code runs: the SIGINT handler writes to a pipe, and a watchdog thread calls `TerminateExecution()`. The REPL then cancels the termination and shows a new prompt.

## Memory management

- **V8 handles.** `Local` handles live in a `HandleScope`. A value kept across callbacks is a `Persistent`/`Global` and is reset when its owner goes away. Handle objects whose native part should die with them use weak callbacks (crypto digests, message ports nobody listens to).
- **libuv.** A handle's memory is freed in its `uv_close()` callback. Request structs (`uv_fs_t`, `uv_write_t`, ...) are heap-allocated and freed in their completion callback.
- **Threads.** `Persistent`s in `thread_local` variables are never reset at thread exit (their isolate is gone by then). A worker resets the ones that matter before disposing its isolate.
- **Known leak.** Buffer memory is never freed: `BufferFinalizer` is a no-op. This is the main item on the memory to-do list (see ROADMAP).

## Build, tests and tooling

- **Build.** The `Makefile` compiles each `.cpp` with `-std=c++20 -Wall -Wextra` and links V8 and libuv. `-MMD` records header dependencies, so a changed header rebuilds its users. `hash.cpp` is built with `-O2`, and `worker_threads.cpp` with `-fno-rtti` (it subclasses V8's serializer delegates, and libv8 has no RTTI). The V8 sandbox defines are detected from the library's symbols.
- **Tests.** `make test` runs the self-tests (`examples/test-*.js`, which also pass under Node.js) and `examples/test-cli.sh`.
- **Lint.** `make lint` runs ESLint on the examples, the tools and the embedded JavaScript, using a processor in `eslint.config.js` that extracts every `R"JS(...)JS"` block. It also compiles every C++ file with `-Werror`. `.clang-format` defines the C++ format; `make format` applies it to changed lines.
- **Types.** `types/*.d.ts` declares the API. `make check-types` type-checks `types/test`, runs the compiled test in FrogJS, and runs `tools/check-types.js`, which compares every declared export and method with the running runtime.
- **CI.** `.github/workflows/build.yml` builds with `WERROR=1` on macOS and runs test, lint and check-types.

## Known limitations

These shape the design today; see ROADMAP for plans.
- No packages (`node_modules`), no ES modules, no `process.nextTick`/`setImmediate`, no `process.stdin` stream.
- `fs` works on UTF-8 strings and reports errors as strings, and its API is a subset.
- `net` sockets are not streams and don't report backpressure.
- Buffers are not `Uint8Array`s and are never freed.
- No HTTPS/TLS, no IPC between processes.
- No sandboxing or permission model: scripts have full access to the file system, network and processes.

---

**Architecture version:** 0.9.0
