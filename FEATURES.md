# FrogJS Features 🚀

## Overview

FrogJS is a lightweight JavaScript runtime built on V8 and libuv, providing Node.js-like functionality with a minimal, clean implementation. This document details all currently implemented features and their usage.

## ✅ Implemented Features

### 1. Core Runtime
- **V8 JavaScript Engine Integration**: Full ECMAScript support through V8
- **Event Loop**: Non-blocking I/O powered by libuv
- **Module System**: CommonJS-style `require()` with caching
- **Error Handling**: Stack traces and detailed error messages
- **Global Objects**: `__filename`, `__dirname` support

### 2. Console API
- `console.log(...args)` - Output to stdout
- `console.error(...args)` - Output to stderr

### 3. Timer API
- `setTimeout(callback, delay)` - Execute callback after delay
- `setInterval(callback, delay)` - Execute callback repeatedly
- `clearTimeout(id)` - Cancel timeout
- `clearInterval(id)` - Cancel interval

### 4. File System API

#### Synchronous Operations
- `fs.readFileSync(path)` - Read file contents synchronously
- `fs.writeFileSync(path, data)` - Write file synchronously

#### Asynchronous Operations
- `fs.readFile(path, callback)` - Read file asynchronously
- `fs.writeFile(path, data, callback)` - Write file asynchronously

### 5. Network API (TCP)
- **TCP Server**: `net.createServer(callback)`
  - `server.listen(port, callback)` - Start listening
  - `server.close()` - Close server

  - `server.address()` - `{ address, family, port }` (useful after `listen(0)`)
  - `net.createServer({ allowHalfOpen: true }, callback)` - Keep writing after the peer ends

- **TCP Client**: `net.connect(port[, host][, callback])` - Resolves hostnames (DNS), IPv4 and IPv6
  - `socket.write(data[, encoding])` - Write a string or Buffer (`'latin1'` writes raw bytes)
  - `socket.end([data])` - Flush pending writes, then close
  - `socket.destroy()` - Close immediately
  - `socket.setEncoding('latin1')` - Receive data as byte strings
  - `socket.on(event, callback)` - `connect`, `data`, `end`, `error`, `close`
  - `socket.remoteAddress` / `socket.remotePort`

### 6. Module System
- **CommonJS Support**: `require()` and `module.exports`
- **Module Caching**: Modules loaded once per session
- **Relative Paths**: Support for `./` and `../` imports
- **Index Files**: Automatic `index.js` resolution
- **File Extension Resolution**: `.js` extension auto-detection
- **Built-in Modules**: `require('fs')`, `require('net')`, `require('http')`, `require('path')`, `require('os')`, `require('events')`, `require('stream')`

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
- `Buffer.alloc(size)`, `Buffer.from(string | array)`, `Buffer.byteLength(string)`, `Buffer.concat(list[, totalLength])`, `Buffer.isBuffer(value)`
- `buf.toString()`, `buf.write(string[, offset])`, `buf.slice([start][, end])`, `buf.length`

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
const fs = require('fs');

// Sync
const data = fs.readFileSync('file.txt');
console.log(data);

// Async
fs.readFile('file.txt', (err, data) => {
    if (err) throw err;
    console.log(data);
});
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

### Completed ✅
- Basic JavaScript execution
- Console API
- Timer API
- File system operations (sync & async)
- TCP server/client
- HTTP server/client
- Module system with caching
- Error handling with stack traces
- Process object (argv, env, exit, cwd, pid, platform, version)
- Buffer class for binary data
- Additional FS operations (mkdir, rmdir, stat, readdir, unlink, existsSync)
- Path module
- OS module
- Events module (EventEmitter)
- Streams (Readable, Writable, Duplex, Transform, PassThrough, pipeline, finished)

### In Progress 🔨
- None currently

### Planned Features ⏳
- `fs.createReadStream` / `fs.createWriteStream`
- REPL mode
- Worker threads
- Native ESM support (import/export)
- TypeScript definitions

## 🧪 Testing

Run example scripts to test features:
```bash
# Console
./build/frogjs examples/hello.js

# Timers
./build/frogjs examples/timers.js

# File system
./build/frogjs examples/fs-sync.js
./build/frogjs examples/fs-async.js

# HTTP
./build/frogjs examples/test-http.js
./build/frogjs examples/http-server.js
./build/frogjs examples/http-client.js

# TCP
./build/frogjs examples/tcp-server.js
./build/frogjs examples/tcp-client.js

# Modules
./build/frogjs examples/modules/main.js

# Path & OS
./build/frogjs examples/test-path.js
./build/frogjs examples/test-os.js
```

## 📊 Performance

- **Startup Time**: ~50-100ms (V8 initialization)
- **Memory Usage**: Minimal overhead
- **Event Loop**: Efficient libuv integration
- **JIT Compilation**: V8 optimizations applied

## 🔧 Build System

### Supported Platforms
- macOS (via Homebrew)
- Linux (manual compilation required)
- Windows (in progress)

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

**Last Updated**: 2025-10-28
**Version**: 0.1.0 (Development)
