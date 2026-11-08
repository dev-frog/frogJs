# FrogJS Architecture

FrogJS is a JavaScript runtime in the mould of Node.js: **V8** runs the JavaScript, **libuv** provides the event loop, I/O and threads, and about 15,000 lines of C++ (half of it JavaScript embedded in C++ files) connect the two and implement the built-in modules. It has no other dependencies: hashing, HTTP parsing and `util.inspect` are all part of the runtime.

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
│   buffer · timers · events · stream · fs · net · http · path · util │
│   crypto · child_process · worker_threads · process · require()    │
├────────────────────────────────────────────────────────────────────┤
│ Native bindings (C++)                                              │
│   console · os · native helpers of the JS modules: file operations,│
│   TCP handles, timers, hashing, encodings, pipes, processes, ports │
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
  os.cpp                                                      (native)
  events.cpp  stream.cpp  http.cpp  path.cpp  util.cpp        (mostly embedded JavaScript)
  buffer.cpp  timers.cpp  fs.cpp  net.cpp  process.cpp  modules.cpp  crypto.cpp
  child_process.cpp  worker_threads.cpp                       (embedded JavaScript + native helpers)
src/utils/
  js_module.cpp      RunJsModule(): runs an embedded module
  errors.cpp         HandleUncaught(), MakeCallback(), rejection tracking, UVException()
  allocator.cpp      the ArrayBuffer allocator shared by all threads
  coverage.cpp       JavaScript coverage for `make coverage`
  encoding.cpp       hex / base64 / latin1 / ... shared by Buffer and crypto
  hash.cpp           MD5, SHA-1, SHA-2, HMAC, PBKDF2
include/frogjs/      headers shared between files (all in namespace frogjs)
  runtime.h  loop.h  worker.h  errors.h  process.h  js_module.h  util.h  buffer.h  encoding.h  hash.h
  allocator.h  coverage.h
  repl.h  line_editor.h  v8_compat.h  version.h
types/               TypeScript declarations of the API (see types/README.md)
examples/            examples and the self-tests (test-*.js, lib/check.js)
benchmark/           benchmarks against Node.js (make bench)
tools/               check-types.js, check-docs.js, coverage.py, leakcheck.sh, fuzz.js
```

## Startup

```
main()
  ├── ParseArgs()             --help / --version exit here; bad options exit with 9
  ├── pick the source         --eval code │ script file │ stdin │ nothing → REPL
  ├── V8::InitializePlatform(), V8::Initialize(), Isolate::New()
  ├── CreateContext()         a context with every built-in installed (below)
  ├── RunMainModule()         a file: load it as a module (ExecuteScript() for -e and stdin)
  ├── RunEventLoop()          until no handle keeps the loop alive, with 'beforeExit' (or StartREPL())
  ├── FinishProcess()         'exit' listeners; the exit code
  ├── TerminateAllWorkers()   stop and join workers that are still running (unref'd)
  ├── RunThreadCleanups()     close what is left (unref'd handles, digests), drain the loop
  └── dispose the isolate, V8 and the loop
```

`frogjs::CreateContext()` (`include/frogjs/runtime.h`) installs the built-ins in dependency order: each later one can use the earlier ones through the global object.

```
console → util → process → Buffer → timers → events → stream (+ process.stdout/stderr) → fs → net
        → http → crypto → path → os → child_process → worker_threads → process (JavaScript half) → require()
```

The REPL's `.clear` and every worker thread call the same function, so all contexts are built the same way.

## Built-in modules

Each `src/bindings/<name>.cpp` exposes `void Setup<Name>(Isolate*, Local<Context>)`, which puts the module on the global object (`fs`, `http`, ...). `require('<name>')` returns that same object; the list of names is in `src/bindings/modules.cpp`.

There are two kinds:

**Native modules** (`os`, and `console`) are C++ functions set as properties of an object.

**Embedded JavaScript modules** (all the others) are JavaScript in a C++ raw string. The source is one function expression; `frogjs::RunJsModule()` (`src/utils/js_module.cpp`) compiles it as `frogjs:<name>` (the name in stack traces) and calls it with the module's dependencies. Its return value is the module.

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
| `Buffer` | encodings, `decodeInto`, `utf8Write` | the `Uint8Array` subclass, a shared pool, binary reads and writes |
| timers | one `uv_timer_t` per timeout, a check/idle pair for immediates | `Timeout`, `Immediate`, argument checks |
| `fs` | one function per operation, run inline or on the thread pool | argument checks, `Stats`, `Dirent`, streams, `fs.promises` |
| `net` | a TCP handle: connect, listen, read, try-write, shutdown, lookup | `Socket` (a Duplex), `Server`, address fallback, errors |
| `path` | none | POSIX path functions |
| `process` | exit, signals, clocks, memory | EventEmitter, `exit()`, `nextTick`, `stdin`; also defines `queueMicrotask`, `performance`, `TextEncoder`, ... |
| `require` | `compile`, `realpath` | the loader: resolution, cache, `node_modules` |
| `events` | none | `EventEmitter` |
| `stream` | byte-string helpers | `Readable`, `Writable`, `Duplex`, `Transform`, `pipeline`, ... |
| `http` | none; it uses `net` | HTTP/1.1 parser and serializer, `Server`, `IncomingMessage`, `ClientRequest`, limits and timeouts, ... |
| `util` | Proxy/Promise/boxed-value inspection | `inspect`, `format`, `promisify` |
| `crypto` | `hash.cpp` digests, `uv_random`, PBKDF2 on the thread pool | `Hash`/`Hmac` streams, validation |
| `child_process` | `uv_spawn`, pipes, a private loop for `spawnSync` | `ChildProcess`, stdio streams, `exec`, `execSync`, ... |
| `worker_threads` | threads, isolates, message ports, serialization | `Worker`, `MessagePort` methods, `MessageChannel` |

C++ code calls back into embedded JavaScript too: `console.log` formats with `util.format` through `frogjs::Format()`, and error reports use `util.inspect` through `frogjs::Inspect()` (`include/frogjs/util.h`).

## The event loop

`frogjs::RunEventLoop()` runs the thread's libuv loop until no active, referenced handle or request remains:

```cpp
DrainTasks(isolate);
while (uv_loop_alive(loop)) {
    uv_run(loop, UV_RUN_ONCE);
    DrainTasks(isolate);   // microtasks, then rejections still unhandled
}
```

Promise jobs (microtasks) run automatically when a callback into JavaScript returns. `process.nextTick` and the `nextTick` inside `stream` drain their queues from a single microtask, so a burst of stream events doesn't allocate a promise each. Ticks and promise reactions share one queue, a difference from Node.js noted in the API reference.

Every asynchronous operation follows the same path:

```
JavaScript call → binding starts a libuv request or handle, keeps the callback in a Global → returns
                                     ⋮
libuv completion on the loop thread → C++ callback: HandleScope
                                     → frogjs::MakeCallback(): call JavaScript in a TryCatch
                                     → an exception goes to HandleUncaught()
```

A handle that keeps the process alive is `uv_ref`'d; `unref()` (timers, sockets, servers, child processes, workers, message ports) makes it stop counting. Work that would block runs on the libuv thread pool: `fs` requests, `pbkdf2`, async `randomBytes`, DNS.

### Writes and reads

`net` tries `uv_try_write()` first: most writes fit in the kernel's buffer, with no copy and no request. What doesn't fit is copied into a queued request whose completion calls the `Writable`'s callback, which is what makes `write()` return `false` and `'drain'` fire. Reads stop when `Readable.push()` returns `false` and start again from `_read()`, so a slow consumer slows the sender.

## Module loading

The loader is JavaScript in `src/bindings/modules.cpp`. Native code only compiles a module wrapper and resolves real paths.

```
require(id)
  ├── a built-in name (or node:name)? → the object captured at startup, so replacing a global can't break it
  ├── a path? → exact file → .js → .json → directory (package.json main → index.js → index.json)
  ├── else: each node_modules up the tree, honoring package.json exports
  ├── in the cache? → its current exports (partial, for a circular require)
  ├── new Module, in the cache before it runs
  ├── .json: parse. Anything else: wrap in (function (exports, require, module, __filename, __dirname) { ... })
  │     (compiled with a line offset of -1, so error lines match the file)
  └── on an exception: remove it from the cache, so requiring it again runs it again
```

A file given on the command line is a module too (`RunMainModule()`). `-e` code, stdin and the REPL run as plain scripts, with `module`, `exports`, `__filename` and `__dirname` as globals. ES modules are not supported.

## Errors

Everything uncaught goes through `frogjs::HandleUncaught()` (`src/utils/errors.cpp`), whether from the main script, a callback through `MakeCallback()`, or a rejection:

1. Termination (`process.exit()` in a worker, `worker.terminate()`, Ctrl+C in the REPL) is ignored.
2. On a worker thread, the error is sent to the parent as an `'error'` event, and the worker ends.
3. `process.on('uncaughtException')` listeners get it, and the program goes on. A listener that throws gives exit code 7.
4. In the REPL it is printed and the session goes on.
5. Otherwise it is printed the way Node.js does (`file:line`, the source line with a caret, the value through `util.inspect`), `'exit'` is emitted, and the process exits with code 1.

**Rejections.** `TrackPromiseRejections()` installs V8's reject callback. A promise rejected without a handler goes on a list, and one that gets a handler comes off it. `DrainTasks()`, after each loop iteration, reports what is left: to `'unhandledRejection'` listeners, or as an uncaught exception.

Errors thrown to JavaScript match Node.js's: the same class, `code` and message (`ERR_INVALID_ARG_TYPE`, `ENOENT`, ...). `frogjs::UVException()` builds the file system ones; `net` builds its own in JavaScript (`connect ECONNREFUSED 127.0.0.1:80`). The self-tests check this by running under both runtimes.

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

- **V8 handles.** `Local` handles live in a `HandleScope`. A value kept across callbacks is a `Global` and is reset when its owner goes away. Handle objects whose native part should die with them use weak callbacks (crypto digests, message ports nobody listens to).
- **Buffers.** A `Buffer` is a `Uint8Array` over an `ArrayBuffer`, so V8 owns the bytes, frees them with the last view, and counts them as external memory for GC decisions. Bounds checks and `this` checks come with it.
- **The allocator.** `frogjs::ArrayBufferAllocator()` is one per process and never destroyed, because a `SharedArrayBuffer` or transferred buffer can outlive the thread that allocated it. It recycles blocks of 512 B to 1 MB by size class, up to 64 MB, because V8's default allocator is slow when V8 is built with its sandbox. A reused block is zeroed unless the caller asked for uninitialized memory, which only `NewBuffer()` does, and it fills the block at once.
- **The Buffer pool.** Small Buffers from `from()`, `concat()` and `allocUnsafe()` are carved from an 8 KB block in JavaScript. A block is zero-filled and each byte is handed out once, so new Buffers never show old data.
- **libuv.** A handle's memory is freed in its `uv_close()` callback. Request structs (`uv_fs_t`, `uv_write_t`, ...) are heap-allocated and freed in their completion callback.
- **Thread end.** Weak callbacks don't run when an isolate is disposed, and unref'd handles are still open. Bindings register with `frogjs::AddThreadCleanup()`, and `RunThreadCleanups()` releases all of it before the loop and isolate go away, on the main thread and in workers. `make leakcheck` verifies this.

## Build, tests and tooling

- **Build.** The `Makefile` compiles each `.cpp` with `-std=c++20 -O2 -Wall -Wextra` into `build/obj/` and links V8 and libuv, found through Homebrew or `V8_PATH`/`LIBUV_PATH`. `-MMD` records header dependencies. Files that subclass V8 classes use `-fno-rtti`, because libv8 has no RTTI. The V8 sandbox defines are detected from the library's symbols. Instrumented builds go in their own directories (`build/asan`, `build/coverage`).
- **Tests.** `make test` runs the self-tests (`examples/test-*.js`, most of which also pass under Node.js), `examples/test-cli.sh`, and `examples/test-repl.py` on a real terminal. A test passes only if it exits with 0 *and* its last line says `0 failed`, so a test that ends early fails.
- **Memory.** `make test-asan` runs the tests under AddressSanitizer and UndefinedBehaviorSanitizer; `make leakcheck` runs a workload under macOS's `leaks`; `test-memory.js` checks that long-running work doesn't grow the process. V8 builds that load PartitionAlloc's allocator shim (Homebrew's 15.x) replace `malloc`, so with them only UndefinedBehaviorSanitizer and `test-memory.js` can run, and the first two say so.
- **Fuzzing.** `make fuzz` sends mutated HTTP over real sockets and random arguments to Buffer, on the sanitizer build.
- **Coverage.** `make coverage` combines `llvm-cov` for the C++ with V8's precise coverage for the embedded JavaScript (`src/utils/coverage.cpp`, through the inspector protocol, only compiled into the coverage build).
- **Benchmarks.** `make bench` compares with Node.js; see [performance](performance.md).
- **Lint.** `make lint` runs ESLint on the examples, the tools and the embedded JavaScript, using a processor in `eslint.config.js` that extracts every `R"JS(...)JS"` block. It also compiles every C++ file with `-Werror`. `.clang-format` defines the C++ format; `make format` applies it to changed lines.
- **Types and docs.** `types/*.d.ts` declares the API. `make check-types` type-checks `types/test`, runs the compiled test in FrogJS, and compares every declared export and method with the running runtime. `make check-docs` checks that each of them is in the API reference.
- **CI.** `.github/workflows/build.yml` runs all of this on macOS.

## Known limitations

See the [roadmap](../ROADMAP.md) for what may come.
- No ES modules, TLS/HTTPS, ciphers, signatures, IPC between processes, global `URL`, or `fetch`.
- `process.nextTick` shares the microtask queue with promises.
- `process.env` is a plain object: changing it doesn't call `setenv()`, though child processes get its current content.
- `fs` paths are UTF-8 strings; there is no `encoding` option for names.
- No sandboxing or permission model: scripts have full access to the file system, network and processes. See the [threat model](../SECURITY.md#threat-model).

---

**Architecture version:** 1.0.0
