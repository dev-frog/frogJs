# FrogJS 🐸

**FrogJS** is a lightweight JavaScript runtime built on V8 and libuv, designed for server-side JavaScript development. Inspired by Node.js architecture, FrogJS provides a minimal yet powerful environment with CommonJS module support, TCP networking, file I/O, and asynchronous operations.

---

## Features ✨

- **V8 JavaScript Engine**: Fast ECMAScript execution with modern JavaScript support
- **Event-Driven I/O**: Non-blocking operations powered by libuv
- **Module System**: CommonJS-style `require()` with caching and relative path support
- **TCP Networking**: Built-in TCP server and client functionality
- **File System**: Both sync and async file operations
- **Timer API**: setTimeout, setInterval, clearTimeout, clearInterval
- **Clean Architecture**: Minimal C++ codebase with clear separation of concerns

---

## Prerequisites 🛠️

Before building FrogJS, ensure you have the following dependencies:

### macOS (Homebrew)
```bash
brew install v8 libuv
```

### Linux (Ubuntu/Debian)
```bash
# V8 requires building from source or using distro packages
sudo apt-get install libuv1-dev
```

### Build Requirements
- **C++ Compiler**: GCC (Linux) or Clang (macOS) with C++17 support
- **Make**: For building via Makefile
- **CMake**: Optional, for cross-platform builds (requires custom V8 config)

---

## Installation 📦

### Clone the Repository
```bash
git clone https://github.com/your-username/frogjs.git
cd frogjs
```

### Build (macOS with Homebrew)
```bash
make clean
make
```

The executable will be created at `./build/frogjs`

### Build (Linux)
Update the Makefile paths to point to your V8 and libuv installations:
```bash
# Edit Makefile with your paths
make
```

---

## Quick Start 🚀

### Hello World
Create `hello.js`:
```javascript
console.log("Hello from FrogJS! 🐸");
```

Run it:
```bash
./build/frogjs examples/hello.js
```

### TCP Server
Create a simple TCP server:
```javascript
const net = require('net');

const server = net.createServer((socket) => {
    socket.on('data', (data) => {
        console.log('Received:', data.toString());
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

### File Operations
```javascript
const fs = require('fs');

// Async read
fs.readFile('example.txt', (err, data) => {
    if (err) throw err;
    console.log(data);
});

// Sync write
fs.writeFileSync('output.txt', 'Hello, FrogJS!');
```

### Modules
```javascript
// math.js
const add = (a, b) => a + b;
module.exports = { add };

// app.js
const math = require('./math');
console.log(math.add(1, 2)); // 3
```

---

## Examples 📚

Check out the `examples/` directory for more samples:
- `examples/timers.js` - Timer API usage
- `examples/fs-sync.js` - Synchronous file operations
- `examples/fs-async.js` - Asynchronous file operations
- `examples/tcp-server.js` - TCP server implementation
- `examples/tcp-client.js` - TCP client implementation
- `examples/modules/` - Module system examples

Run examples:
```bash
./build/frogjs examples/timers.js
./build/frogjs examples/tcp-server.js
./build/frogjs examples/modules/main.js
```

---

## Project Structure 📁

```
frogjs/
├── src/
│   ├── core/
│   │   └── runtime.cpp       # Main entry point
│   └── bindings/
│       ├── timers.cpp        # Timer API
│       ├── fs.cpp           # File system API
│       ├── net.cpp          # TCP networking
│       └── modules.cpp      # Module system
├── examples/                # Example scripts
├── docs/                    # Documentation
├── include/                 # Header files (planned)
├── Makefile                 # Build configuration
├── CMakeLists.txt          # CMake build (in development)
├── README.md               # This file
├── FEATURES.md             # Detailed feature list
└── note.md                 # Development notes
```

---

## Current Status 🎯

### ✅ Implemented
- Core runtime with V8 integration
- Console API (log, error)
- Timer API (setTimeout, setInterval, etc.)
- File system operations (sync & async)
- TCP server/client networking
- Module system (require, exports, caching)
- Error handling with stack traces

### ⏳ Planned
- Process object (argv, env, exit, cwd)
- Buffer class for binary data
- Additional FS operations (mkdir, stat, readdir)
- HTTP module
- REPL mode
- Worker threads
- Windows support

---

## Documentation 📖

- **[FEATURES.md](FEATURES.md)** - Comprehensive feature documentation
- **[note.md](note.md)** - Development guide and implementation notes
- **[steps.md](steps.md)** - Step-by-step implementation guide

---

## Building from Source 🔧

### Makefile Build (Recommended for macOS)
```bash
make clean
make
./build/frogjs your-script.js
```

### CMake Build (In Development)
```bash
mkdir -p build && cd build
cmake ..
make
./frogjs your-script.js
```

---

## Performance 📊

- **Startup Time**: ~50-100ms (V8 initialization)
- **Memory Usage**: Minimal overhead
- **Event Loop**: Efficient libuv integration
- **JIT Compilation**: Full V8 optimizations

---

## Contributing 🤝

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.

### Development Setup
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly with the examples
5. Submit a pull request

---

## License 📜

MIT License - See LICENSE file for details

---

## Acknowledgments 🙏

- **V8 Team** - For the amazing JavaScript engine
- **libuv Team** - For the excellent I/O library
- **Node.js** - For architectural inspiration

---

**Version**: 0.1.0 (Development)
**Status**: Active Development
**Last Updated**: 2025-10-28

## License 📜

_FrogJS_ is licensed under the MIT License. See the LICENSE file for more details.

## Support 🐸

If you enjoy using FrogJS, consider giving it a ⭐ on GitHub! For questions or feedback, open an issue or reach out to us.
