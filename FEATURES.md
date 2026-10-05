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

- **TCP Client**: `net.connect(port, host)`
  - `socket.write(data)` - Write data
  - `socket.end()` - Close connection
  - `socket.on(event, callback)` - Event handling

### 6. Module System
- **CommonJS Support**: `require()` and `module.exports`
- **Module Caching**: Modules loaded once per session
- **Relative Paths**: Support for `./` and `../` imports
- **Index Files**: Automatic `index.js` resolution
- **File Extension Resolution**: `.js` extension auto-detection
- **Built-in Modules**: `require('fs')`, `require('net')`, `require('path')`, `require('os')`

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
- Module system with caching
- Error handling with stack traces
- Process object (argv, env, exit, cwd, pid, platform, version)
- Buffer class for binary data
- Additional FS operations (mkdir, rmdir, stat, readdir, unlink, existsSync)
- Path module
- OS module

### In Progress 🔨
- None currently

### Planned Features ⏳
- HTTP module
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
