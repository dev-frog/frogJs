# Changelog

FrogJS follows [Semantic Versioning](https://semver.org). See the [API stability policy](docs/api-stability.md).

## 1.0.0

The first stable release. It fixes the API by matching Node.js in the places FrogJS differed, so these breaking changes don't have to happen later. If you are coming from 0.9, read [Migrating](#migrating-from-09).

### Breaking changes

- **`fs` contents**: without an encoding, `readFile` and `readFileSync` return a Buffer, not a string. Pass `'utf8'` for text.
- **`fs` errors** are `Error` objects with `code`, `errno`, `syscall` and `path`, not strings.
- **`fs.Stats`**: `isFile()` and `isDirectory()` are methods, not booleans.
- **`net` sockets** are Duplex streams. Data arrives as Buffers unless `setEncoding()` was called, and `socket.on()` adds a listener instead of replacing the previous one.
- **`server.listen()`** reports failures such as `EADDRINUSE` as an `'error'` event, not a throw. `server.close()` waits for connections to end. `socket.write()` and `socket.end()` after the end of a socket are stream errors.
- **`Buffer`** is a `Uint8Array` subclass. `slice()` shares memory with the original, as before, and `Buffer.from(arrayBuffer)` now does too. Bad sizes and offsets throw `RangeError`s with codes, not strings; `slice()` with negative or too large positions clamps instead of throwing.
- **Uncaught errors in callbacks end the process** with exit code 1, as they already did in the main script. Before, they were printed and the program went on. Use `process.on('uncaughtException')` to take over.
- **Unhandled promise rejections end the process** with exit code 1. Before, they were ignored. Use `process.on('unhandledRejection')` to take over.
- **Timers return objects**, not numbers. `clearTimeout()` still takes both, and `+timer` gives the number.
- **`process.argv[0]` and `[1]` are absolute paths.**
- **The main script is a module**: top-level `var` isn't global anymore, and `this` is `module.exports`. `__filename` and `__dirname` come from the module wrapper, not from globals.
- **Errors from `events.on()`'s iterator**: `throw()` returns `undefined`; the following `next()` rejects, as in Node.js.

### Added

- `fs`: `appendFile`, `rename`, `copyFile`, `rm`, `lstat`, `fstat`, `realpath`, `access`, `open`, `close`, `read`, `write`, `createReadStream`, `createWriteStream`, `fs.promises`, `fs.constants`, `Dirent`, `mkdir` with `recursive`, encodings, flags and modes.
- `net`: `Socket` and `Server` classes, `createConnection`, `isIP`, `isIPv4`, `isIPv6`, `setTimeout`, `setNoDelay`, `setKeepAlive`, `ref`, `unref`, `maxConnections`, `getConnections`, `bytesRead`, `bytesWritten`, and more, with backpressure both ways.
- `Buffer`: indexing, `alloc` with a fill, `allocUnsafe`, `compare`, `equals`, `copy`, `fill`, `indexOf`, `lastIndexOf`, `includes`, `isEncoding`, `toJSON`, `subarray`, and `read*`/`write*` for integers and floats.
- `process`: an EventEmitter with `'exit'`, `'beforeExit'`, `'uncaughtException'`, `'unhandledRejection'`, `'warning'` and signals; `exitCode`, `nextTick`, `stdin`, `hrtime`, `uptime`, `memoryUsage`, `cpuUsage`, `kill`, `chdir`, `umask`, `arch`, `execPath`, `ppid`, `emitWarning`.
- Timers: `setImmediate`, `clearImmediate`, extra arguments, `ref`, `unref`, `hasRef`, `refresh`.
- Globals: `global`, `queueMicrotask`, `structuredClone`, `performance`, `TextEncoder`, `TextDecoder`.
- Modules: `node_modules`, `package.json` `main` and `exports`, `.json` files, `node:` names, `buffer`, `timers`, `process` and `module` as modules, `require.resolve`, `require.cache`, `require.main`, circular requires with partial exports, top-level `return`.
- `path`: `relative`, `parse`, `format`, `posix`.
- HTTP: `write()` reports backpressure, with `'drain'`; `server.requestTimeout`.
- `install.sh`: install on macOS and Linux (and Windows through WSL).
- [API stability policy](docs/api-stability.md), [support policy](docs/support-policy.md), [SECURITY.md](SECURITY.md), [security review](docs/security-audit.md), [benchmarks](docs/performance.md).
- Tooling: `make test-asan`, `leakcheck`, `coverage`, `bench`, `fuzz`, `check-docs`.

### Fixed

- **`fs.readFile` returned only the first few bytes of a file**: as many as the descriptor's number.
- `fs.writeFile` never closed its file descriptor.
- `fs.readFileSync` stopped at the first NUL byte, and reading a directory returned an empty string.
- **Buffer memory was never freed**, so anything allocating Buffers grew without limit.
- `Buffer.alloc(-1)` and similar aborted the process. A Buffer method on another object read out of bounds.
- `setTimeout(fn, -1)` never fired, and the timer callbacks ignored extra arguments.
- Circular `require()` overflowed the stack, and requiring an empty file failed.
- A `SharedArrayBuffer` from a worker that had ended was freed through a deleted allocator.
- `crypto` digest objects still alive at exit leaked.
- `process.stdout.end(data, callback)` called the callback twice.
- Strings over V8's length limit aborted the process in `buf.toString()`.
- `process.exitCode` was ignored.
- A huge `Buffer.alloc()` killed the process on V8 builds without the sandbox. Buffers and ArrayBuffers are now limited to 32 GiB on every build, and larger ones throw a `RangeError`.
- `Intl` and `toLocaleString()` threw, and `new Intl.DateTimeFormat()` aborted the process, because V8's ICU data file wasn't found.
- The code didn't compile against current V8 (15.8), which is what `brew install v8` gives today.
- See the [security review](docs/security-audit.md) for the HTTP and argument handling fixes.

### Performance

The build is now optimized (`-O2`), and it was unoptimized before except for hashes. Socket writes avoid copying with `uv_try_write`, small Buffers come from a pool, ArrayBuffer blocks are recycled, SHA-256 uses the ARMv8 instructions, and `path` runs in JavaScript. See [docs/performance.md](docs/performance.md).

### Migrating from 0.9

```javascript
// fs: ask for text, and use the error's code
const text = fs.readFileSync(file, 'utf8');                    // was: fs.readFileSync(file)
fs.readFile(file, 'utf8', (err, data) => { /* ... */ });       // was: fs.readFile(file, (err, data) => ...)
if (fs.statSync(file).isDirectory()) { /* ... */ }             // was: .isDirectory
try { fs.readFileSync(file); } catch (err) { err.code; }       // err was a string

// net: text needs an encoding; several listeners can coexist
socket.setEncoding('utf8');
socket.on('data', (text) => { /* ... */ });

// listen errors are events
server.on('error', (err) => { /* EADDRINUSE */ });
server.listen(port);

// Errors you relied on being printed and ignored now end the process
process.on('uncaughtException', (err) => { /* log it */ });
process.on('unhandledRejection', (reason) => { /* log it */ });
```

## 0.9.0 and earlier

See the [roadmap](ROADMAP.md) and the [Git history](https://github.com/dev-frog/frogJs/commits/main).
