# FrogJS Development Roadmap

## 📊 Current Status: Version 1.0.0

### ✅ Completed (v0.1.0)
- Core V8 runtime integration
- Console API (log, error)
- Timer API (setTimeout, setInterval, clearTimeout, clearInterval)
- Basic File System (readFileSync, writeFileSync, readFile, writeFile)
- TCP server/client networking
- Module system (require, exports, caching)
- Event loop with libuv
- Error handling with stack traces
- GitHub Actions CI/CD
- Release automation

### ✅ Completed (v0.2.0)
- Process object (argv, env, exit, cwd, pid, platform, version)
- Buffer class (alloc, from, byteLength, toString, write, slice)
- Enhanced file system (mkdir, stat, readdir, unlink, rmdir, existsSync)

### ✅ Completed (v0.3.0)
- Path module (join, resolve, normalize, dirname, basename, extname, isAbsolute, sep, delimiter)
- OS module (platform, arch, cpus, hostname, release, type, totalmem, freemem, homedir, tmpdir, EOL)
- `require('path')`, `require('os')`, `require('fs')`, `require('net')` return the built-in modules
- V8 15 compatibility

### ✅ Completed (v0.4.0)
- HTTP module: `http.createServer`, `http.request`, `http.get`
- Server, IncomingMessage, ServerResponse and ClientRequest classes
- HTTP/1.1 keep-alive, pipelining, chunked encoding, 100-continue
- TCP module: hostname resolution, `server.address()`, `socket.destroy()`, `close` event, binary-safe writes

### ✅ Completed (v0.5.0)
- Events module: Node-compatible `EventEmitter`, `events.once`, `events.on`, `errorMonitor`
- Stream module: `Readable`, `Writable`, `Duplex`, `Transform`, `PassThrough`, `pipeline`, `finished`, `stream.promises`
- `pipe()` with backpressure, async iteration, `Readable.from()`
- HTTP classes are `EventEmitter`s; `IncomingMessage` is a `Readable` (`req.pipe(res)`, `for await`)
- `Buffer.concat()`, `Buffer.isBuffer()`

### ✅ Completed (v0.6.0)
- REPL: line editing, persistent history, tab completion, multi-line input, `.help`/`.exit`/`.clear`/`.break`/`.load`/`.save`, `_`/`_error`, Ctrl+C interrupts running code
- CLI: `--version`, `--help`, `--eval`, `--print`, `--check`, `--interactive`, `-` (stdin)
- Node.js-style uncaught error reports (file:line, source line, caret, stack) and exit code 1
- Util module: `util.inspect` (Node-identical output), `util.format`, `promisify`, `inherits`, `isDeepStrictEqual`
- `console.log` formats objects with `util.inspect`; `console.info`/`debug`/`warn`
- `process.versions`, `process.execArgv`

### ✅ Completed (v0.7.0)
- Crypto module: `createHash` and `createHmac` (md5, sha1, sha224, sha256, sha384, sha512), `pbkdf2`/`pbkdf2Sync`
- `randomBytes`, `randomInt`, `randomUUID`, `getRandomValues`, `timingSafeEqual`, `getHashes`
- Hash and Hmac objects are Transform streams (`source.pipe(hash)`)
- Buffer encodings: `hex`, `base64`, `base64url`, `latin1`/`binary`, `ascii` in `Buffer.from`, `buf.toString([encoding[, start[, end]]])` and `Buffer.byteLength`; `Buffer.from(buffer | TypedArray | ArrayBuffer)`

### ✅ Completed (v0.8.0)
- Child process module: `spawn`, `exec`, `execFile`, `spawnSync`, `execSync`, `execFileSync`, `ChildProcess`
- Child stdio as streams (`child.stdin` Writable, `child.stdout`/`stderr` Readable), `stdio` pipe/inherit/ignore/fd, `shell`, `cwd`, `env`, `timeout`, `killSignal`, `maxBuffer`, `input`, `kill()`; `util.promisify(exec)` resolves `{ stdout, stderr }`
- Worker threads module: `Worker` (file or `eval`), `isMainThread`, `parentPort`, `workerData`, `threadId`, `MessageChannel`, `MessagePort`, `receiveMessageOnPort`
- Structured-clone message passing; `SharedArrayBuffer` + `Atomics` shared between threads; transferable `ArrayBuffer`s and `MessagePort`s
- `process.stdout` / `process.stderr` streams; `util.promisify.custom`

### ✅ Completed (v0.9.0)
- TypeScript declarations for the globals and all 11 built-in modules (`types/`), with type tests and a runtime check (`make check-types`)
- Linting: ESLint for the examples, tools and the JavaScript embedded in C++ (`make lint-js`); warning-free C++ under `-Werror` (`make lint-cpp`, CI builds with `WERROR=1`)
- Formatting: `.clang-format`, `.editorconfig`, `make format` / `make format-check`, and a [style guide](docs/style-guide.md)
- Rewritten [contributing guide](docs/contributing.md) and [architecture document](docs/architecture.md)
- `make test` / `make check`, header dependency tracking, and CI running test + lint + type checks
- Module fixes: nested `require()` resolves from the requiring module's directory; `module.exports` can be any value; the main script has `module`/`exports`

### ✅ Completed (v1.0.0)
- See [Version 1.0.0](#version-100---stable-release) below and the [changelog](CHANGELOG.md)
- Breaking, to match Node.js before the API is frozen: `fs` contents and errors, `net` sockets as streams, `Buffer` as a `Uint8Array`, timers as objects, uncaught callback errors and rejections ending the process

---

## 🎯 Development Roadmap

### Version 0.2.0 - Core Utilities ✅ Completed

**Priority: HIGH**
**Estimated Effort: 2-3 weeks**

#### Process Object
```cpp
// src/bindings/process.cpp
void SetupProcess(Isolate* isolate, Local<Context> context);
```

**Features:**
- `process.argv` - Command line arguments array
- `process.env` - Environment variables object
- `process.exit(code)` - Graceful process termination
- `process.cwd()` - Current working directory
- `process.pid` - Process ID
- `process.platform` - OS platform
- `process.version` - FrogJS version

**Implementation Notes:**
- Parse command line arguments in main()
- Access environment variables using getenv()
- Platform detection using preprocessor macros
- Version from embedded constant

#### Buffer Class
```cpp
// src/bindings/buffer.cpp
void SetupBuffer(Isolate* isolate, Local<Context> context);
```

**Features:**
- `Buffer.alloc(size)` - Allocate zero-filled buffer
- `Buffer.from(string)` - Create buffer from string
- `Buffer.from(array)` - Create buffer from array
- `Buffer.byteLength(string)` - Get byte length
- `buf.toString(encoding)` - Convert to string
- `buf.write(string)` - Write string to buffer
- `buf.slice(start, end)` - Create slice view
- `buf.length` property - Buffer size

**Implementation Notes:**
- Use V8's ArrayBuffer backing
- Handle UTF-8 encoding
- Memory management with V8 isolate

#### Enhanced File System
```cpp
// Extend src/bindings/fs.cpp
```

**New Functions:**
- `fs.mkdir(path, callback)` - Create directory
- `fs.mkdirSync(path)` - Synchronous mkdir
- `fs.stat(path, callback)` - Get file stats
- `fs.statSync(path)` - Synchronous stat
- `fs.readdir(path, callback)` - Read directory
- `fs.readdirSync(path)` - Synchronous readdir
- `fs.unlink(path, callback)` - Delete file
- `fs.unlinkSync(path)` - Synchronous unlink
- `fs.existsSync(path)` - Check file existence
- `fs.rmdir(path, callback)` - Remove directory
- `fs.rmdirSync(path)` - Synchronous rmdir

**Implementation Notes:**
- Use libuv filesystem functions
- Return stat objects with size, mtime, etc.
- Handle error codes properly

---

### Version 0.3.0 - Path & OS Utilities ✅ Completed

**Priority: MEDIUM**
**Estimated Effort: 1-2 weeks**

#### Path Module
```cpp
// src/bindings/path.cpp
void SetupPath(Isolate* isolate, Local<Context> context);
```

**Features:**
- `path.join(...paths)` - Join path segments
- `path.resolve(...paths)` - Resolve to absolute path
- `path.dirname(path)` - Get directory name
- `path.basename(path)` - Get base name
- `path.extname(path)` - Get file extension
- `path.isAbsolute(path)` - Check if absolute
- `path.normalize(path)` - Normalize path
- `path.sep` / `path.delimiter` - Platform separators

**Implementation Notes:**
- POSIX semantics matching Node.js `path.posix`; verified against Node with 2,400+ input combinations
- Non-string arguments throw `TypeError`

#### OS Module
```cpp
// src/bindings/os.cpp
void SetupOS(Isolate* isolate, Local<Context> context);
```

**Features:**
- `os.platform()` - Operating system platform
- `os.arch()` - CPU architecture
- `os.cpus()` - CPU info
- `os.hostname()` - System hostname
- `os.release()` - OS release
- `os.type()` - OS type
- `os.totalmem()` - Total memory
- `os.freemem()` - Free memory
- `os.homedir()` - Home directory
- `os.tmpdir()` - Temp directory
- `os.EOL` - End-of-line marker

**Implementation Notes:**
- Built on libuv (`uv_cpu_info`, `uv_os_uname`, `uv_os_homedir`, `uv_os_tmpdir`, ...)
- Output matches Node.js `os` on the same machine

---

### Version 0.4.0 - HTTP Module ✅ Completed

**Priority: MEDIUM-HIGH**
**Estimated Effort: 3-4 weeks**

#### HTTP Module
```cpp
// src/bindings/http.cpp
void SetupHTTP(Isolate* isolate, Local<Context> context);
```

**Features:**
- `http.createServer(callback)` - Create HTTP server
- `http.request(options, callback)` - Make HTTP request
- `http.get(url, callback)` - Simple GET request
- `Server` class with listen(), close()
- `ServerRequest` class with headers, method, url
- `ServerResponse` class with write(), end(), setHeader()
- Basic HTTP/1.1 protocol support

**Implementation Notes:**
- Written in JavaScript (embedded in `http.cpp`) on top of the TCP module, which runs in `latin1` mode so the protocol layer works on bytes
- Incremental HTTP/1.x parser: headers, Content-Length and chunked bodies, trailers, pipelining
- Rejects request smuggling (Content-Length + Transfer-Encoding) and header injection
- Routing is left to user code (see `examples/http-server.js`)
- Not included: HTTPS, client connection pooling (Agent)

---

### Version 0.5.0 - Events & Streams ✅ Completed

**Priority: MEDIUM**
**Estimated Effort: 2-3 weeks**

#### Events Module
```cpp
// src/bindings/events.cpp
void SetupEvents(Isolate* isolate, Local<Context> context);
```

**Features:**
- `EventEmitter` base class
- `emitter.on(event, listener)` - Register listener
- `emitter.emit(event, ...args)` - Emit event
- `emitter.removeListener(event, listener)` - Remove listener
- `emitter.removeAllListeners(event)` - Clear listeners

#### Stream Basics
```cpp
// src/bindings/stream.cpp
```

**Features:**
- `Readable` base class
- `Writable` base class
- `stream.pipe(destination)` - Pipe streams
- Basic backpressure handling

**Implementation Notes:**
- Both modules are written in JavaScript embedded in `events.cpp` / `stream.cpp` (loaded by `src/utils/js_module.cpp`), like `http`
- Names follow Node.js (`Readable`/`Writable`, plus `Duplex`, `Transform`, `PassThrough`), not the WHATWG `ReadableStream`/`WritableStream`
- Stream internals follow the Node.js algorithms (flowing/paused modes, `highWaterMark`, `'drain'`, `autoDestroy`); `examples/test-events.js` and `examples/test-streams.js` pass under both FrogJS and Node.js
- Strings are not converted to Buffers, so string lengths count characters; `setEncoding()` supports utf8, latin1, ascii and hex
- The HTTP module's internal emitter was replaced by the public one, and `IncomingMessage` now extends `Readable`
- Not included yet: `fs.createReadStream`/`createWriteStream`, backpressure from TCP sockets (`net` sockets are not streams yet)

---

### Version 0.6.0 - REPL & CLI Improvements ✅ Completed

**Priority: MEDIUM**
**Estimated Effort: 2-3 weeks**

#### REPL Mode
```cpp
// src/core/repl.cpp
void StartREPL(Isolate* isolate);
```

**Features:**
- Interactive JavaScript shell
- Command history (using readline)
- Tab completion
- Special commands (.help, .exit, .clear)
- Pretty-print objects
- Syntax error recovery

#### CLI Improvements
- `--version` flag
- `--eval` flag for inline code
- `--print` flag (print result)
- Better error messages
- Help text

**Implementation Notes:**
- `src/core/repl.cpp` runs the REPL on the libuv loop (stdin is a `uv_poll` handle), so timers and sockets keep working between prompts and async `console.log` output is printed above the prompt
- Line editing is a small built-in editor (`src/core/line_editor.cpp`) instead of GNU readline (GPL) or libedit, whose callback API on macOS doesn't report Ctrl+D or the current line; no new dependency
- History is saved to `~/.frogjs_repl_history` (`FROGJS_REPL_HISTORY` overrides it, empty disables it), 1000 lines
- Tab completion covers globals, keywords, top-level `let`/`const`/`class` names, properties of `a.b.c` chains (prototype chain included) and REPL commands
- Incomplete input (`Unexpected end of input`, unterminated template literals) continues on a `...` prompt; `{ ... }` is evaluated as an object literal
- Ctrl+C during evaluation stops the script through `Isolate::TerminateExecution` from a watchdog thread; `.clear` builds a fresh context with all bindings
- `util.inspect` is a port of Node's layout rules (line breaking, array grouping, quoting, circular refs) and `examples/test-util.js` passes under both runtimes
- `require()` now propagates errors thrown by a module (previously it printed them and returned `undefined`), and module line numbers in errors match the file
- Like Node.js, an uncaught error in the main script exits with code 1 without running the event loop; errors in timer and fs callbacks are reported with their stack but don't stop the process yet
- Not included yet: top-level `await` in the REPL, `.editor` mode, unhandled-rejection reporting

---

### Version 0.7.0 - Crypto & Security ✅ Completed

**Priority: LOW-MEDIUM**
**Estimated Effort: 2 weeks**

#### Crypto Module
```cpp
// src/bindings/crypto.cpp
void SetupCrypto(Isolate* isolate, Local<Context> context);
```

**Features:**
- `crypto.createHash(algorithm)` - Create hash
- `hash.update(data)` - Update hash
- `hash.digest(encoding)` - Get digest
- Support: md5, sha1, sha256, sha512

#### Utilities
- `crypto.randomBytes(size)` - Random bytes
- `crypto.pbkdf2()` - Password-based key derivation

**Implementation Notes:**
- MD5, SHA-1, SHA-2, HMAC and PBKDF2 are implemented in `src/utils/hash.cpp` instead of linking OpenSSL, so there is still no dependency beyond V8 and libuv. The SHA-2/MD5 constants were derived from their definitions (roots of primes, `sin`), and outputs match Node.js/OpenSSL for every algorithm at all padding-boundary lengths, byte-at-a-time updates and 1 MB inputs
- The API layer is JavaScript embedded in `src/bindings/crypto.cpp`; argument checks, error codes and messages match Node.js (`ERR_CRYPTO_HASH_FINALIZED`, `ERR_CRYPTO_INVALID_DIGEST`, `ERR_OUT_OF_RANGE`, ...), and `examples/test-crypto.js` passes under both runtimes
- Random bytes come from the OS CSPRNG through `uv_random()`; async `randomBytes` and `pbkdf2` run on the libuv thread pool
- `hash.o` is built with `-O2` (the rest of the build is unoptimized). PBKDF2-SHA256 with 100k iterations takes ~60 ms, against ~15 ms in Node.js, which uses OpenSSL's assembly and the CPU's SHA instructions
- Differences from Node.js: `pbkdf2Sync(..., keylen 0)` returns an empty Buffer (Node.js 24 throws "Deriving bits failed"); `getHashes()` lists only the six supported hashes
- Not included: ciphers (`createCipheriv`), signatures and key objects, `scrypt`, `hkdf`, `randomFill`/`randomFillSync`, Web Crypto `subtle`

---

### Version 0.8.0 - Advanced Features ✅ Completed

**Priority: LOW**
**Estimated Effort: 4-6 weeks**

#### Child Process
```cpp
// src/bindings/child_process.cpp
```

**Features:**
- `child_process.spawn()` - Spawn process
- `child_process.exec()` - Execute command
- `child_process.execSync()` - Sync exec
- Process I/O streams

#### Worker Threads
- Basic threading support
- Message passing between workers
- SharedArrayBuffer support

**Implementation Notes:**
- `spawn()` uses `uv_spawn` with libuv pipes wrapped as `stream.Readable`/`Writable`; the `*Sync` functions run the child on a private libuv loop, so timers and I/O of the main loop are not run while they block (as in Node.js). Events (`spawn`, `exit`, `close`, `error`), error objects (`spawn x ENOENT`, `Command failed: ...`, `ETIMEDOUT`, `ENOBUFS`) and argument errors match Node.js; `examples/test-child-process.js` passes under both runtimes
- Each `Worker` is a thread with its own V8 isolate, context and libuv loop. Per-thread state that used to be global (the event loop, timers, the module cache, cached templates) is now `thread_local`, and bindings use `frogjs::Loop()` instead of `uv_default_loop()`
- Messages are serialized with V8's `ValueSerializer` (structured clone: Map, Set, Date, RegExp, BigInt, Error, typed arrays, ...). `SharedArrayBuffer` backing stores are shared, transferred `ArrayBuffer`s are detached, and `MessagePort`s can be transferred to other threads. Buffers arrive as `Uint8Array`, as in Node.js
- Uncaught errors in a worker (also in its callbacks) end the worker and become the `Worker`'s `error` event, with properties such as `err.code` kept; `process.exit()` in a worker ends only that worker; `terminate()` interrupts running JavaScript. Unref'd workers are stopped and joined before the process exits
- `examples/test-worker-threads.js` passes under both runtimes; the new code was also run under AddressSanitizer
- Differences from Node.js: the main script is not wrapped in a module function, so it can't `return` at the top level (workers started from a file run the same way); worker `stdout`/`stderr` write straight to the process's (no `worker.stdout` streams)
- Not included: IPC (`fork()`, `stdio: 'ipc'`, `child.send()`), `AbortSignal` options, `resourceLimits`, `worker.getHeapSnapshot()`, `BroadcastChannel`, environment data, ES module workers

---

### Version 0.9.0 - TypeScript & Tooling ✅ Completed

**Priority: LOW**
**Estimated Effort: 2-3 weeks**

#### TypeScript Definitions
```typescript
// types/index.d.ts
declare module 'fs' { ... }
declare module 'net' { ... }
declare module 'http' { ... }
// etc.
```

#### Tooling
- Linting support
- Formatting guidelines
- Contributing guidelines
- Architecture documentation

**Implementation Notes:**
- `types/*.d.ts` describe FrogJS rather than Node.js. Where they differ, the declarations follow FrogJS:
  - `fs` errors are strings, contents are strings, and `Stats.isFile`/`isDirectory` are booleans.
  - Buffers aren't indexable.
  - `net` sockets aren't streams.
  - `setTimeout` requires a delay.
  - `process` isn't an EventEmitter.
- The declarations are checked three ways:
  - `types/test/usage.ts` compiles under `--strict`, is compiled to JavaScript and runs in FrogJS (31 runtime checks).
  - `types/test/errors.ts` lists 13 mistakes that must not compile.
  - `tools/check-types.js` reads the declarations with the TypeScript compiler API and verifies, in a running FrogJS, that every declared export and class method exists. There are no undeclared exports.
- The ESLint config has a processor that lints each `R"JS(...)JS"` block in `src/*.cpp` in place, with errors at their `.cpp` line. Turning it on found an undefined variable in `examples/test-fs-enhanced.js`, plus dead assignments and needless escapes in `http`, `stream` and `util`.
- The C++ is warning-free under `-Wall -Wextra`. Results ignored on purpose are marked with `(void)` and a comment.
- `.clang-format` is Google style with 4 spaces and 120 columns. Existing code isn't reformatted wholesale: `make format` (`git clang-format`) formats only the lines that differ from `main`.
- Writing the tests found two module bugs, now fixed:
  - `require('./x')` inside a module in a subdirectory resolved from the main script's directory.
  - `module.exports` had to be an object.
  - The main script also gained `module`/`exports`, so TypeScript's CommonJS output runs as the entry point.
- Dev dependencies (`package.json`): `typescript` 5.9 (for its compiler API; TypeScript 7 doesn't have one), `eslint` 10, `@eslint/js`, `globals`
- Not included: clang-tidy (no compile database yet), publishing the declarations to npm, a CMake build that matches the Makefile

---

### Version 1.0.0 - Stable Release

**Priority: CRITICAL**

#### 1.0.0 Goals
| Goal | Status | Where |
|------|--------|-------|
| Complete feature parity with planned v0.x features | ✅ Done | Every feature listed under v0.2.0 to v0.9.0 exists and is tested. Most "not included yet" items that would have changed the API were also done, so they don't break it later |
| Comprehensive test suite | ✅ Done | 21 self-tests, the CLI and REPL tests, under sanitizers too; the old ones that only printed now assert |
| Performance optimizations | ✅ Done | [docs/performance.md](docs/performance.md) |
| Memory leak fixes | ✅ Done | Buffers, file descriptors, digests; `make leakcheck` finds 0 leaks with V8 13.5, and RSS stays flat over long runs |
| Security audit | ⚠️ Internal review done | [docs/security-audit.md](docs/security-audit.md). Not an independent, third-party audit |
| Complete documentation | ✅ Done | `make check-docs` ties the API reference to the declarations, and `make check-types` ties those to the runtime |
| API stability guarantees | ✅ Written | [docs/api-stability.md](docs/api-stability.md) |
| Long-term support commitment | ⚠️ Draft | [docs/support-policy.md](docs/support-policy.md): needs the maintainer's confirmation, since it is a promise of their time |

**Implementation Notes:**
- Node.js is the reference: new tests run under both runtimes, and where they disagreed FrogJS changed.
- **Memory.** `Buffer` became a `Uint8Array` subclass instead of native storage with a no-op finalizer, which fixes the leak at its root and brings bounds checks, indexing and GC accounting. `fs.writeFile` leaked its descriptor, and digests leaked at exit.
- **Bugs found by the audit:** `fs.readFile` read only as many bytes as the descriptor's number. `Buffer.alloc(-1)` aborted the process, and a method called on another object read out of bounds. `setTimeout(fn, -1)` never fired. A worker's `SharedArrayBuffer` was freed through an allocator that was already deleted. `readFileSync` stopped at the first NUL. Circular `require()` overflowed the stack. Empty modules failed. See the [changelog](CHANGELOG.md).
- **Coverage** is measured for both languages: V8's precise coverage reaches the embedded JavaScript through the inspector protocol, in a coverage-only build.
- Not done, and possible later: ES modules, TLS/HTTPS, ciphers and signatures, `fork()` with IPC, global `URL`, `fetch`, an official Linux build, top-level `await` in the REPL.

---

## 🛠️ Technical Debt & Improvements

### High Priority
- [x] Memory management review ([architecture](docs/architecture.md#memory-management))
- [x] Error handling standardization (`HandleUncaught`, `MakeCallback`, `Error` objects with `code`)
- [x] Thread safety analysis (the allocator; [security review](docs/security-audit.md))
- [x] Performance profiling ([benchmarks](docs/performance.md))

### Medium Priority
- [ ] Code refactoring for modularity
- [ ] Better separation of concerns
- [ ] Internal API documentation
- [x] Benchmark suite (`make bench`)

### Low Priority
- [ ] Code style consistency (reformat the older C++ with clang-format, then enforce `make format-check`)
- [ ] Better comments
- [ ] Architecture diagrams
- [x] Development tools (`test-asan`, `leakcheck`, `coverage`, `fuzz`, `check-docs`)

---

## 📈 Success Metrics

### Version 0.2.0 Success Criteria
- ✅ Process object fully functional
- ✅ Buffer class handles all common operations
- ✅ File system has complete path/directory operations
- ✅ No memory leaks in new features
- ✅ Examples for all new features
- ✅ Updated documentation

### Version 1.0.0 Success Criteria
Measured on macOS on Apple Silicon, 2026-11-09:

| Criterion | Status | Result |
|-----------|--------|--------|
| All planned v0.x features complete | ✅ Met | |
| 90%+ test coverage | ✅ Met | 91.5% of lines: JavaScript 91.3%, C++ 91.8% (`make coverage COVERAGE_MIN=90`, in CI). Worker threads' JavaScript isn't recorded, so it counts as uncovered |
| Performance within 2x of Node.js for equivalent operations | ⚠️ Mostly | 19 of 23 benchmarks in the last run. `stream-pipe` and `sha256-small` are about 2.9x and `buffer-hex-base64` about 2.2x; `timers` and `sha256-large` sit right at 2x. See [performance](docs/performance.md) |
| No known memory leaks | ✅ Met, with a gap | `make leakcheck`: 0 leaks with V8 13.5. It can't run against Homebrew's current V8 (15.8), where only `test-memory.js` (no growth) applies. On macOS only |
| API stability for 6+ months | ❌ Not met | It can't be met yet: 1.0.0 itself changes APIs, so the clock starts with this release |
| Production-ready error handling | ✅ Met | Uncaught errors and rejections, exit codes, `Error` objects with `code`, hostile input handled |
| Complete API documentation | ✅ Met | `make check-docs`: all declared names documented |

---

## 🗓️ Timeline Estimates

- **v0.2.0**: ✅ Completed
- **v0.3.0**: ✅ Completed
- **v0.4.0**: ✅ Done
- **v0.5.0**: ✅ Done
- **v0.6.0**: ✅ Done
- **v0.7.0**: ✅ Done
- **v0.8.0**: ✅ Done
- **v0.9.0**: ✅ Done
- **v1.0.0**: ✅ Done

---

## 🚀 What Could Come Next

New features after 1.0 are additive: see the [API stability policy](docs/api-stability.md). Candidates:
- ES modules
- TLS and `https`
- Ciphers (`createCipheriv`/`createDecipheriv`, AES-GCM), `scrypt`, `hkdf`, signatures
- `child_process.fork()` with an IPC channel
- Global `URL` and `URLSearchParams`, `AbortController`, `fetch`
- Top-level `await` and `.editor` in the REPL
- `node:assert`, `node:readline`, `node:dns`
- An official Linux build, with SHA-NI on x86-64
- The benchmarks still over 2x: `stream` overhead, `crypto.createHash` setup, hex and base64 with SIMD
- Reformat the older C++ with clang-format, then enforce `make format-check` in CI; clang-tidy
