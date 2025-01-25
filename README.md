# FrogJS 🐸

**FrogJS** is a lightweight, fast, and modern JavaScript runtime designed to make server-side JavaScript development simple and efficient. Inspired by Node.js, FrogJS aims to provide a minimalistic yet powerful environment for building scalable applications.

---

## Features ✨

- **Fast Execution**: Built on top of the V8 JavaScript engine for blazing-fast performance.
- **Event-Driven Architecture**: Non-blocking I/O operations powered by Libuv.
- **Modular Design**: Easy-to-use APIs for file I/O, networking, timers, and more.
- **Cross-Platform**: Works on Windows, macOS, and Linux.
- **Lightweight**: Minimal overhead, perfect for microservices and edge computing.

---

## Installation 🛠️

To install FrogJS, follow these steps:

### Prerequisites

- **C++ Compiler**: Ensure you have a C++ compiler installed (e.g., GCC, Clang, or MSVC).
- **CMake**: Required for building the project.
- **V8 and Libuv**: These dependencies are included in the build process.

### Build Instructions

1. Clone the repository:

    ```bash
    git clone https://github.com/your-username/frogjs.git
    cd frogjs
    ```

2. Build the project:

    ```bash
    mkdir build && cd build
    cmake ..
    make

    ```

3. Run the executable:

    ```bash
        ./bin/frogjs
    ```

## Quick Start 🚀

Running a Simple Script

Create a file named `hello.js`:

```js
console.log("Hello from FrogJS! 🐸");
```

Run the script using FrogJS:

```js
./bin/frogjs hello.js
```

Creating an HTTP Server
Here's an example of a simple HTTP server:

```js
const { createServer } = require('net');

const server = createServer((req, res) => {
  res.writeHead(200, { 'Content-Type': 'text/plain' });
  res.end('Hello, World!');
});

server.listen(3000, () => {
  console.log('Server running at http://localhost:3000');
});

```

Run the server:

```js
./bin/frogjs http-server.js
```

## License 📜

_FrogJS_ is licensed under the MIT License. See the LICENSE file for more details.

## Support 🐸

If you enjoy using FrogJS, consider giving it a ⭐ on GitHub! For questions or feedback, open an issue or reach out to us.
