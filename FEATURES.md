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

### In Progress 🔨
- None currently

### Planned Features ⏳
- Process object (argv, env, exit, cwd)
- Buffer class for binary data
- Additional FS operations (mkdir, stat, readdir)
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
