# FrogJS Features 🚀

## Overview

FrogJS is a lightweight JavaScript runtime built on V8 and libuv, providing Node.js-like functionality with a minimal, clean implementation. This document details all currently implemented features and their usage.

## ✅ Implemented Features

### 1. Core Runtime
- **V8 JavaScript Engine Integration**: Full ECMAScript support through V8
- **Event Loop**: Non-blocking I/O powered by libuv
- **Module System**: CommonJS-style `require()` with caching
- **Error Handling**: Node-style reports of uncaught errors (file:line, source line with caret, stack) and exit code 1
- **Global Objects**: `__filename`, `__dirname` support

### 2. Console API
- `console.log(...args)`, `console.info()`, `console.debug()` - Output to stdout
- `console.error(...args)`, `console.warn()` - Output to stderr
- Arguments are formatted with `util.format`: objects are pretty-printed (`{ a: 1 }`, `[ 1, 2 ]`) and `%s %d %i %f %j %o %O %c %%` are supported

### 3. Timer API
- ✅ `setTimeout`, `setInterval`, `setImmediate` (with extra arguments), and their `clear*` functions
- ✅ `Timeout` and `Immediate` objects: `ref()`, `unref()`, `hasRef()`, `refresh()`, `close()`
- ✅ `process.nextTick`, `queueMicrotask`

### 4. File System API
- ✅ Whole files: `readFile`, `writeFile`, `appendFile`, binary-safe, with encodings and flags
- ✅ Directories: `mkdir` (with `recursive`), `readdir` (with `withFileTypes`), `rmdir`, `rm` (with `recursive`, `force`)
- ✅ Metadata: `stat`, `lstat`, `fstat`, `realpath`, `access`, `existsSync`; `Stats` and `Dirent` classes
- ✅ `rename`, `copyFile`, `unlink`
- ✅ Descriptors: `open`, `close`, `read`, `write`
- ✅ Streams: `createReadStream`, `createWriteStream`
- ✅ `fs.promises`; sync, callback and promise forms behave the same
- ✅ Errors are `Error` objects with `code`, `errno`, `syscall`, `path`

### 5. Network API (TCP)
- ✅ `net.Socket`: a Duplex stream, with backpressure in both directions
- ✅ `net.Server`: an EventEmitter, `listen()` with options, `close()` waiting for connections, `maxConnections`, `getConnections()`
- ✅ `net.connect` / `createConnection`: DNS resolution, trying each address
- ✅ `setTimeout`, `setNoDelay`, `setKeepAlive`, `ref`, `unref`, addresses and byte counts
- ✅ `net.isIP`, `isIPv4`, `isIPv6`
- ✅ Errors with `code`, `errno`, `syscall`, `address`, `port`

### 6. Module System
- ✅ `require()` following Node.js's algorithm: files, `.json`, directories, `node_modules`, scoped packages
- ✅ `package.json`: `main`, and `exports` for exact subpaths and conditions
- ✅ `node:` prefix; `buffer`, `timers`, `process`, `module` as modules
- ✅ `require.resolve`, `require.cache`, `require.main`, `createRequire`, `module.paths`, ...
- ✅ Circular requires get partial exports; failed modules aren't cached
- ✅ The main script is a module (top-level `return`, its own scope)
- ❌ ES modules

### 7. Path API
POSIX path utilities matching Node.js `path.posix` (available as global `path` or `require('path')`)
- `path.join(...paths)` - Join and normalize path segments
- `path.resolve(...paths)` - Resolve to an absolute path (relative to `process.cwd()`)
- `path.normalize(path)` - Resolve `.`/`..` segments and duplicate slashes
- `path.dirname(path)` - Directory portion of a path
- `path.basename(path[, ext])` - Last portion of a path, optionally without `ext`
- `path.extname(path)` - File extension (e.g. `.js`)
- `path.isAbsolute(path)` - Whether a path is absolute
- `path.sep` (`/`) and `path.delimiter` (`:`)

### 8. OS API
Operating system information via libuv (available as global `os` or `require('os')`)
- `os.platform()` / `os.type()` / `os.release()` / `os.arch()`
- `os.cpus()` - Array of `{ model, speed, times: { user, nice, sys, idle, irq } }`
- `os.hostname()` / `os.homedir()` / `os.tmpdir()`
- `os.totalmem()` / `os.freemem()` - Memory in bytes
- `os.EOL` - End-of-line marker (`\n`)

### 9. HTTP API
HTTP/1.1 server and client (available as global `http` or `require('http')`), written in JavaScript on top of the TCP module
- **Server**: `http.createServer([options][, requestListener])`
  - `server.listen([port][, host][, callback])`, `server.close([callback])`, `server.address()`
  - `server.closeIdleConnections()`, `server.closeAllConnections()`
  - `server.keepAliveTimeout` (5s), `server.headersTimeout` (60s)
  - Events: `request`, `listening`, `connection`, `close`, `error`, `clientError`, `checkContinue`
- **IncomingMessage** (`req` on the server, `res` on the client) - a `stream.Readable`, so it can be piped and read with `for await`
  - `method`, `url`, `statusCode`, `statusMessage`, `headers` (lower-cased), `rawHeaders`, `trailers`, `httpVersion`, `socket`
  - Events: `data` (UTF-8 strings; `setEncoding('latin1')` for raw bytes), `end`, `aborted`, `close`
  - Body chunks are buffered until a `data` listener is attached, `resume()` is called or it is piped
- **ServerResponse**
  - `statusCode`, `statusMessage`, `setHeader()`, `getHeader()`, `getHeaders()`, `hasHeader()`, `removeHeader()`
  - `writeHead(status[, message][, headers])`, `write(chunk)`, `end([chunk][, callback])`, `writeContinue()`
  - `headersSent`, `finished`, `destroy()`; events `finish`, `close`
  - Works as a `pipe()`/`pipeline()` destination (`write()` always returns `true`: no backpressure yet)
- **Client**: `http.request(url | options[, options][, callback])`, `http.get(...)`
  - Options: `hostname`/`host`, `port`, `path`, `method`, `headers`, `timeout`
  - `req.write()`, `req.end()`, `req.setHeader()`, `req.setTimeout()`, `req.destroy()`, `req.abort()`
  - Events: `response`, `information` (1xx), `error`, `timeout`, `close`
- **Protocol**
  - Keep-alive (HTTP/1.1 default) and request pipelining on the server
  - Chunked transfer encoding in both directions, including trailers
  - Content-Length added automatically when `end(data)` is called without prior writes
  - `Date` and `Connection` headers added automatically
  - Bodies may be strings or Buffers; Content-Length is counted in bytes
  - `Expect: 100-continue` answered automatically
  - Malformed requests get `400`, oversized headers `431`, and a handler that throws gets `500`
  - Requests with both Content-Length and Transfer-Encoding are rejected (request smuggling)
  - CR/LF in header names, values and request paths are rejected (header injection)
- `http.METHODS`, `http.STATUS_CODES`
- All HTTP classes are `EventEmitter`s (listener exceptions are reported rather than thrown, so a throwing handler gets a `500`)
- **Limitations**: plain HTTP only (no HTTPS), and the client opens one connection per request (no Agent/pooling)

### 10. Events API
Node.js-compatible `EventEmitter` (`require('events')`, also the global `events`)
- `on` / `addListener`, `once`, `prependListener`, `prependOnceListener`
- `off` / `removeListener`, `removeAllListeners([name])`
- `emit(name, ...args)` - returns whether there were listeners; listener exceptions propagate
- `listeners()`, `rawListeners()`, `listenerCount(name[, listener])`, `eventNames()`
- `setMaxListeners(n)`, `getMaxListeners()`, `EventEmitter.defaultMaxListeners` (10, with a leak warning)
- Special events: `'error'` (throws when unhandled), `'newListener'`, `'removeListener'`, `EventEmitter.errorMonitor`
- `events.once(emitter, name)` - Promise of the next event; `events.on(emitter, name)` - async iterator
- Supports `class X extends EventEmitter`, `EventEmitter.call(this)` and string or symbol event names

### 11. Stream API
Node.js-style streams (`require('stream')`, also the global `stream`)
- **Readable**: `push()`, `unshift()`, `read([n])`, `pause()`, `resume()`, `isPaused()`, `setEncoding()`, `pipe()`, `unpipe()`, `destroy()`
  - Flowing (`'data'`) and paused (`'readable'` + `read()`) modes, `for await...of`, `Readable.from(iterable)`
  - Events: `data`, `readable`, `end`, `close`, `error`, `pause`, `resume`
- **Writable**: `write()`, `end()`, `cork()`, `uncork()`, `setDefaultEncoding()`, `destroy()`; `_write`, `_writev`, `_final`
  - Events: `drain`, `finish`, `close`, `error`, `pipe`, `unpipe`
- **Duplex**, **Transform** (`_transform`, `_flush`) and **PassThrough**
- Options: `highWaterMark`, `objectMode`, `encoding`, `emitClose`, `autoDestroy`, `allowHalfOpen`, plus `read`/`write`/`writev`/`final`/`transform`/`flush`/`destroy` implementations
- **Backpressure**: `write()` returns `false` at the `highWaterMark` and `'drain'` follows; `pipe()` pauses the source until then
- `stream.pipeline(...streams, callback)` - pipe a chain, destroying every stream if one fails
- `stream.finished(stream[, options], callback)` - notification when a stream ends, finishes, errors or closes early
- `stream.promises.pipeline()` / `stream.promises.finished()`
- **Differences from Node.js**: strings reach `_write()` as strings and their `highWaterMark` length is counted in characters; `setEncoding()` supports `utf8`, `latin1`/`binary`, `ascii` and `hex`

### 12. Buffer
- ✅ A `Uint8Array` subclass: indexing, iteration, freed by the GC
- ✅ `alloc`, `allocUnsafe`, `from`, `concat`, `compare`, `byteLength`, `isBuffer`, `isEncoding`
- ✅ `toString`, `write`, `slice`, `subarray`, `equals`, `compare`, `copy`, `fill`, `indexOf`, `lastIndexOf`, `includes`, `toJSON`
- ✅ `read*`/`write*` for 8 to 64-bit integers and floats, both byte orders
- ✅ Encodings: `utf8`, `hex`, `base64`, `base64url`, `latin1`/`binary`, `ascii`

### 13. Util API
`require('util')`, also the global `util`
- `util.inspect(value[, options])` - same output as Node.js: line breaking at 80 columns, arrays of more than 6 items in columns, quoting, `[Circular *1]` references, class names, getters, `Map`/`Set`/typed arrays/`Promise`/`Proxy`/`Error`/`Buffer`
  - Options: `depth` (2), `colors`, `showHidden`, `maxArrayLength`, `maxStringLength`, `breakLength`, `compact`, `sorted`
  - Objects can customize their output with `[util.inspect.custom](depth, options, inspect)`
- `util.format(format, ...args)`, `util.formatWithOptions(options, format, ...args)`
- `util.promisify(fn)`, `util.inherits(ctor, superCtor)`, `util.isDeepStrictEqual(a, b)`, `util.stripVTControlCharacters(str)`

### 14. Crypto API
`require('crypto')`, also the global `crypto`. Implemented without OpenSSL; outputs match Node.js.
- `crypto.createHash(algorithm)` - md5, sha1, sha224, sha256, sha384, sha512 (names are case-insensitive; `SHA-256` and `RSA-SHA256` work too)
  - `hash.update(data[, inputEncoding])`, `hash.digest([encoding])`, `hash.copy()`
- `crypto.createHmac(algorithm, key)` - `hmac.update()`, `hmac.digest([encoding])`
- Hash and Hmac are Transform streams: `write()`/`end()` then `read()` the digest, or `source.pipe(hash)`
- `crypto.pbkdf2(password, salt, iterations, keylen, digest, callback)` (thread pool) and `crypto.pbkdf2Sync()`
- `crypto.randomBytes(size[, callback])`, `crypto.randomInt([min, ]max[, callback])`, `crypto.randomUUID()`, `crypto.getRandomValues(typedArray)` - from the OS CSPRNG
- `crypto.timingSafeEqual(a, b)` - constant-time comparison
- `crypto.getHashes()`
- Errors use Node.js codes and messages (`ERR_CRYPTO_HASH_FINALIZED`, `ERR_CRYPTO_INVALID_DIGEST`, `ERR_INVALID_ARG_TYPE`, `ERR_OUT_OF_RANGE`, ...)
- **Not included**: ciphers, signatures, key objects, `scrypt`, Web Crypto `subtle`

### 15. Child Process API
`require('child_process')`. Behaves like Node.js (events, errors and messages are the same).
- `spawn(command[, args][, options])` - returns a `ChildProcess`: `pid`, `stdin` (Writable), `stdout`/`stderr` (Readable), `stdio`, `exitCode`, `signalCode`, `killed`, `kill([signal])`, `ref()`/`unref()`; events `spawn`, `exit`, `close`, `error`
  - Options: `cwd`, `env`, `argv0`, `stdio` (`'pipe'`, `'inherit'`, `'ignore'`, a file descriptor, or an array of these), `shell`, `detached`, `uid`, `gid`, `timeout`, `killSignal`
- `exec(command[, options][, callback])` and `execFile(file[, args][, options][, callback])` - buffered output; `encoding`, `timeout`, `maxBuffer`, `killSignal`; `util.promisify()` gives `{ stdout, stderr }`
- `spawnSync()`, `execSync()`, `execFileSync()` - block until the child exits; `input`, `timeout`, `maxBuffer`, `encoding`, `stdio`; failures throw `Command failed: ...` errors with `status`, `signal`, `stdout`, `stderr`
- **Not included**: `fork()` and IPC (`stdio: 'ipc'`, `child.send()`), `AbortSignal` options

### 16. Worker Threads API
`require('worker_threads')`. Each `Worker` runs in its own V8 isolate with its own event loop, on its own thread.
- `new Worker(filename | code, { eval, workerData, argv, transferList })` - `postMessage()`, `terminate()` (Promise of the exit code), `ref()`/`unref()`, `threadId`; events `online`, `message`, `error`, `exit`
- `isMainThread`, `parentPort`, `workerData`, `threadId`
- `MessageChannel`, `MessagePort` (`postMessage(value[, transferList])`, `on('message')`, `onmessage`, `start()`, `close()`, `ref()`/`unref()`), `receiveMessageOnPort(port)`
- Messages are structured clones (objects, arrays, Map, Set, Date, RegExp, BigInt, Error, typed arrays, Buffers as `Uint8Array`)
- `SharedArrayBuffer` memory is shared between threads (use `Atomics`); `ArrayBuffer`s and `MessagePort`s can be transferred
- Uncaught errors in a worker become its `error` event; `process.exit()` in a worker ends only that worker
- **Not included**: `resourceLimits`, `worker.stdout`/`stderr` streams (worker output goes straight to the process's), `BroadcastChannel`, ES module workers

### 17. REPL & Command Line
- `frogjs` with no script (and a terminal) starts the REPL
  - Line editing (arrows, Home/End, Ctrl+A/E/B/F/K/U/W/L, Alt+B/F), history in `~/.frogjs_repl_history` (`FROGJS_REPL_HISTORY`), tab completion
  - Multi-line input, `{ ... }` evaluated as an object literal, colored results (disable with `NO_COLOR`)
  - `_` / `_error`, commands `.help` `.exit` `.clear` `.break` `.load` `.save`
  - Ctrl+C clears the line or interrupts running code; Ctrl+D or Ctrl+C twice exits
  - The event loop keeps running between prompts
- Options: `-e/--eval`, `-p/--print`, `-c/--check`, `-i/--interactive`, `-v/--version`, `-h/--help`, `-` (script from stdin), `--`
- Invalid options exit with code 9; uncaught errors in the main script exit with code 1

### 18. TypeScript Declarations
`types/index.d.ts` declares the globals (`process`, `Buffer`, timers, `require`, ...) and every built-in module. See [types/README.md](types/README.md).
- They follow FrogJS where it differs from Node.js, so the compiler rejects code that would fail at run time:
  - `fs` errors are strings and `Stats.isFile` is a boolean.
  - Buffers can't be indexed.
  - Sockets are not streams.
  - `setTimeout` needs a delay.
- They are checked three ways:
  - `types/test/usage.ts` type-checks under `--strict`, and runs in FrogJS after compiling.
  - `types/test/errors.ts` must be rejected.
  - `tools/check-types.js` finds declared exports and methods that are missing at run time.

### 19. Development Tooling
- `make test`: every self-test plus the CLI test
- `make lint`: ESLint (`eslint.config.js`) on the examples, the tools and the JavaScript embedded in `src/*.cpp` (reported at its `.cpp` line), and every C++ file compiled with `-Werror`
- `make format` / `make format-check`: clang-format (`.clang-format`) on the C++ lines changed since `main` (`git clang-format`)
- `make check-types`: the TypeScript checks above
- `make check`: all of it, as CI runs it (CI also builds with `make WERROR=1`)
- `.editorconfig`; header dependency tracking in the Makefile; [contributing guide](docs/contributing.md) and [style guide](docs/style-guide.md)

## 📖 Usage Examples

### Console
```javascript
console.log("Hello, FrogJS!");
console.error("Error occurred");
```

### Timers
```javascript
setTimeout(() => {
    console.log("Delayed execution");
}, 1000);

const interval = setInterval(() => {
    console.log("Every second");
}, 1000);

clearInterval(interval);
```

### File System
```javascript
const text = fs.readFileSync('data.txt', 'utf8');
const bytes = fs.readFileSync('logo.png');   // a Buffer

fs.readFile('data.txt', 'utf8', (err, data) => {
    if (err) return console.error(err.code);
    console.log(data);
});

const list = await fs.promises.readdir('.');
fs.createReadStream('in').pipe(fs.createWriteStream('out'));
```

### HTTP Server
```javascript
const http = require('http');

const server = http.createServer((req, res) => {
    if (req.method === 'POST') {
        let body = '';
        req.on('data', (chunk) => body += chunk);
        req.on('end', () => res.end(`Received ${body.length} characters\n`));
        return;
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ path: req.url }));
});

server.listen(3000);
```

### HTTP Client
```javascript
const http = require('http');

const req = http.request({ hostname: 'localhost', port: 3000, path: '/', method: 'POST' }, (res) => {
    res.on('data', (chunk) => console.log(chunk));
});
req.on('error', (err) => console.error(err.message));
req.end('hello');
```

### TCP Server
```javascript
const net = require('net');

const server = net.createServer((socket) => {
    socket.on('data', (data) => {
        console.log('Received:', data);
        socket.write('Echo: ' + data);
    });

    socket.on('end', () => {
        console.log('Client disconnected');
    });
});

server.listen(3000, () => {
    console.log('Server listening on port 3000');
});
```

### TCP Client
```javascript
const net = require('net');

const client = net.connect(3000, 'localhost', () => {
    console.log('Connected to server');
    client.write('Hello, server!');
});

client.on('data', (data) => {
    console.log('Received:', data.toString());
});

client.on('end', () => {
    console.log('Disconnected from server');
});
```

### Modules
```javascript
// math.js
const add = (a, b) => a + b;
module.exports = { add };

// main.js
const math = require('./math');
console.log(math.add(1, 2)); // 3
```

### Path
```javascript
const path = require('path');
path.join('/foo', 'bar', '../baz');      // '/foo/baz'
path.resolve('src', 'index.js');         // '<cwd>/src/index.js'
path.basename('/a/b/file.js', '.js');    // 'file'
path.extname('archive.tar.gz');          // '.gz'
```

### Events
```javascript
const EventEmitter = require('events');

class Job extends EventEmitter {}
const job = new Job();
job.on('progress', (pct) => console.log(`${pct}%`));
job.once('done', () => console.log('finished'));
job.emit('progress', 50);
job.emit('done');
```

### Streams
```javascript
const http = require('http');
const { Readable, Transform, pipeline } = require('stream');

// In an HTTP server: stream the request body back, upper-cased
http.createServer((req, res) => {
    const upper = new Transform({
        transform(chunk, encoding, callback) {
            callback(null, String(chunk).toUpperCase());
        },
    });
    pipeline(req, upper, res, (err) => {
        if (err) console.error(err.message);
    });
}).listen(3000);

// Iterate any readable
(async () => {
    for await (const chunk of Readable.from(['a', 'b', 'c'])) console.log(chunk);
})();
```

### OS
```javascript
const os = require('os');
console.log(`${os.type()} ${os.release()} (${os.arch()})`);
console.log(`${os.cpus().length} CPUs, ${Math.round(os.totalmem() / 1024 ** 3)} GB RAM`);
console.log('Home:', os.homedir());
```

## 🏗️ Architecture

### Core Components
- **runtime.cpp**: Main entry point, V8 initialization
- **bindings/**: C++ to JavaScript bridges
- **event loop**: libuv integration for async operations

### Technologies
- **V8**: JavaScript execution engine
- **libuv**: Asynchronous I/O library
- **C++17**: Modern C++ standard

## 🎯 Current Status

Version 1.0.0. Everything above is implemented and tested. See the [changelog](CHANGELOG.md) for what changed, the [roadmap](ROADMAP.md) for what was checked for 1.0.0, and the [API stability policy](docs/api-stability.md) for what is promised.

## 🧪 Testing

```bash
make test          # 21 self-tests, the CLI test and a REPL test on a real terminal
make test-asan     # the same under AddressSanitizer and UndefinedBehaviorSanitizer
make leakcheck     # native leaks at exit
make coverage      # line coverage of the C++ and the embedded JavaScript
make fuzz          # random input for the HTTP parser and Buffer
make check         # lint, tests, type checks and the API reference check
```

Self-tests are `examples/test-*.js`. Where FrogJS implements a Node.js API they also run under Node.js, which is how expected values, error codes and event orders are verified.

## 📊 Performance

See [docs/performance.md](docs/performance.md) for `make bench` results against Node.js.

## 🔧 Build System

### Supported Platforms
- ✅ macOS on Apple Silicon: tested, released and supported
- ⚠️ macOS on Intel, Linux: should build, not tested
- ❌ Windows

See the [support policy](docs/support-policy.md#platforms).

### Build Methods
- **Makefile**: Direct compilation with g++
- **CMake**: Cross-platform build support

## 🤝 Compatibility

### Node.js Compatibility
FrogJS aims for CommonJS compatibility but is not fully Node.js compatible. Focus is on core functionality rather than complete ecosystem parity.

### JavaScript Standards
- Full ES6+ support through V8
- CommonJS module system
- Async/await support (via V8)

---

**Last Updated**: 2026-10-31
**Version**: 1.0.0
