![FrogJS Logo](assets/frogjs2.png)

[![Build](https://github.com/dev-frog/frogJs/actions/workflows/build.yml/badge.svg)](https://github.com/dev-frog/frogJs/actions/workflows/build.yml)
[![Release](https://github.com/dev-frog/frogJs/actions/workflows/release.yml/badge.svg)](https://github.com/dev-frog/frogJs/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/dev-frog/frogJs?include_prereleases&sort=semver)](https://github.com/dev-frog/frogJs/releases/latest)

**FrogJS** is a lightweight JavaScript runtime built on V8 and libuv, designed for server-side JavaScript development. Inspired by Node.js architecture, FrogJS provides a minimal yet powerful environment with CommonJS module support, HTTP and TCP networking, file I/O, and asynchronous operations.

---

## Features

- **V8 JavaScript Engine**: Fast ECMAScript execution with modern JavaScript support
- **Event-Driven I/O**: Non-blocking operations powered by libuv
- **Module System**: CommonJS `require()` following Node.js's algorithm: `node_modules`, `package.json` (`main`, `exports`), JSON, circular requires
- **HTTP Server & Client**: Node-compatible `http` module (keep-alive, chunked encoding, pipelining)
- **Events & Streams**: Node-compatible `EventEmitter` and `stream` module (Readable, Writable, Transform, pipe with backpressure)
- **TCP Networking**: `net` sockets are Duplex streams with real backpressure; servers are EventEmitters
- **File System**: Node-compatible `fs`: sync, callback and promise forms, read and write streams, `Error` objects with `code`
- **Timers**: `setTimeout`, `setInterval`, `setImmediate`, `process.nextTick`, `queueMicrotask`, with `ref()`/`unref()`
- **Process & Buffer**: `process` as an EventEmitter (`'exit'`, `'uncaughtException'`, signals, `stdin`); `Buffer` as a `Uint8Array` subclass
- **Path & OS Utilities**: Node-compatible `path` and `os` modules
- **REPL & CLI**: Interactive shell with history and tab completion, `--eval`/`--print`/`--check`, Node-style error reports
- **Util**: `util.inspect` and `util.format` with the same output as Node.js (used by `console.log`)
- **Crypto**: Node-compatible hashes and HMAC (md5, sha1, sha2), PBKDF2, secure random bytes/UUIDs, `timingSafeEqual`, with no OpenSSL dependency
- **Child Processes**: `spawn`/`exec`/`execFile` with stdio streams, and `execSync`/`spawnSync`/`execFileSync`
- **Worker Threads**: `Worker`s on their own threads, structured-clone message passing, `MessageChannel`, `SharedArrayBuffer` + `Atomics`
- **TypeScript Definitions**: `types/` declares every global and built-in module, checked against the runtime
- **Clean Architecture**: Minimal C++ codebase with clear separation of concerns

---

## Prerequisites

Before building FrogJS, ensure you have the following dependencies:

### macOS (Homebrew)
```bash
brew install v8 libuv
```

### Linux
Not tested for 1.0: see the [support policy](docs/support-policy.md#platforms). The code has no macOS-only parts outside `#if` branches, but needs a V8 you build or obtain yourself:
```bash
sudo apt-get install libuv1-dev
make V8_PATH=/path/to/v8 LIBUV_PATH=/usr
```

### Build Requirements
- **C++ Compiler**: GCC (Linux) or Clang (macOS) with C++17 support
- **Make**: For building via Makefile
- **CMake**: Optional, for cross-platform builds (requires custom V8 config)

---

## Installation

### Download a Prebuilt Release (macOS Apple Silicon)

Prebuilt binaries are published on the [Releases page](https://github.com/dev-frog/frogJs/releases) for every version tag.

```bash
brew install v8 libuv   # runtime dependencies (linked dynamically)

VERSION=0.5.0   # pick a version from the Releases page
curl -LO https://github.com/dev-frog/frogJs/releases/download/v$VERSION/frogjs-$VERSION-macos-arm64.tar.gz
curl -LO https://github.com/dev-frog/frogJs/releases/download/v$VERSION/frogjs-$VERSION-macos-arm64.tar.gz.sha256
shasum -a 256 -c frogjs-$VERSION-macos-arm64.tar.gz.sha256

tar -xzf frogjs-$VERSION-macos-arm64.tar.gz
./frogjs-$VERSION-macos-arm64/frogjs your-script.js
```

### Clone the Repository
```bash
git clone https://github.com/dev-frog/frogJs.git
cd frogJs
```

### Build (macOS with Homebrew)
```bash
make clean
make
```

The executable will be created at `./build/frogjs`

### Build (other installs)
The Makefile finds Homebrew's V8 and libuv. Otherwise, pass the install prefixes:
```bash
make V8_PATH=/path/to/v8 LIBUV_PATH=/path/to/libuv
```

---

## Quick Start

### Hello World
Create `hello.js`:
```javascript
console.log("Hello from FrogJS! 🐸");
```

Run it:
```bash
./build/frogjs examples/hello.js
```

### REPL
Run `frogjs` with no script to start an interactive shell:
```
$ ./build/frogjs
Welcome to FrogJS v1.0.0.
Type ".help" for more information.
> const server = { port: 3000, routes: ['/', '/api'] }
undefined
> server
{ port: 3000, routes: [ '/', '/api' ] }
> process.ver<Tab>
process.version   process.versions
```

- **Editing**: arrow keys, Home/End, Ctrl+A/E/K/U/W, Ctrl+L to clear the screen
- **History**: Up/Down, saved to `~/.frogjs_repl_history` (set `FROGJS_REPL_HISTORY` to change it, or to an empty string to disable it)
- **Tab completion** for globals, keywords, your `let`/`const` names, object properties and REPL commands
- **Multi-line input**: unfinished code continues on a `...` prompt
- `_` holds the last result and `_error` the last error
- **Commands**: `.help`, `.exit`, `.clear` (fresh context), `.break`, `.load <file>`, `.save <file>`
- **Ctrl+C** clears the line or stops running code (e.g. an endless loop); press it twice on an empty line, or Ctrl+D, to exit
- Timers and servers keep running between prompts

### Command Line
```bash
frogjs script.js [args...]        # run a script
frogjs -e "console.log(1 + 1)"    # evaluate code
frogjs -p "os.cpus().length"      # evaluate and print the result
frogjs -c script.js               # syntax check only
frogjs -i -e "var x = 1"          # run code, then start the REPL
echo "console.log('hi')" | frogjs # read the script from stdin
frogjs --version                  # v1.0.0
frogjs --help                     # all options
```

Uncaught errors are reported like Node.js does, and the process exits with code 1:
```
app.js:3
  return user.name;
              ^

TypeError: Cannot read properties of undefined (reading 'name')
    at getName (app.js:3:15)
    at app.js:6:1
```

### HTTP Server
```javascript
const http = require('http');

const server = http.createServer((req, res) => {
    res.setHeader('Content-Type', 'text/plain');
    res.end(`You requested ${req.method} ${req.url}\n`);
});

server.listen(3000, () => {
    console.log('Listening on http://localhost:3000');
});
```

### HTTP Client
```javascript
const http = require('http');

http.get('http://localhost:3000/hello', (res) => {
    let body = '';
    res.on('data', (chunk) => body += chunk);
    res.on('end', () => console.log(res.statusCode, body));
});
```

### Events & Streams
```javascript
const http = require('http');
const EventEmitter = require('events');
const { Transform, pipeline } = require('stream');

const bus = new EventEmitter();
bus.on('upload', (bytes) => console.log(`received ${bytes} characters`));

// Stream each request body back upper-cased, with backpressure handled by pipeline()
http.createServer((req, res) => {
    let size = 0;
    const upper = new Transform({
        transform(chunk, encoding, callback) {
            size += chunk.length;
            callback(null, String(chunk).toUpperCase());
        },
    });
    pipeline(req, upper, res, (err) => {
        if (!err) bus.emit('upload', size);
    });
}).listen(3000);
```

### Crypto
```javascript
const crypto = require('crypto');

crypto.createHash('sha256').update('hello').digest('hex');
// '2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824'
crypto.createHmac('sha256', 'secret').update('message').digest('base64');

// Password hashing on the thread pool
const salt = crypto.randomBytes(16);
crypto.pbkdf2('my password', salt, 100000, 32, 'sha256', (err, key) => {
    console.log(key.toString('hex'));
});

crypto.randomUUID();   // e.g. '3b241101-e2bb-4255-8caf-4136c566a962'
crypto.randomInt(1, 7); // a dice roll
crypto.timingSafeEqual(Buffer.from(tokenA), Buffer.from(tokenB));
```

### Child Processes
```javascript
const { spawn, exec, execSync } = require('child_process');

const ls = spawn('ls', ['-l', '/tmp']);
ls.stdout.pipe(process.stdout);
ls.on('close', (code) => console.log(`ls exited with ${code}`));

exec('git rev-parse HEAD', (err, stdout) => console.log(stdout.trim()));
const version = execSync('uname -r', { encoding: 'utf8' });

// Write to a child's stdin
const sort = spawn('sort');
sort.stdout.on('data', (data) => console.log(data.toString()));
sort.stdin.end('pear\napple\nfig\n');
```

### Worker Threads
```javascript
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');

if (isMainThread) {
    const shared = new Int32Array(new SharedArrayBuffer(4));
    const worker = new Worker(__filename, { workerData: { shared: shared.buffer } });
    worker.on('message', (msg) => console.log('worker says', msg, 'counter', shared[0]));
    worker.on('exit', (code) => console.log('worker exited', code));
} else {
    const counter = new Int32Array(workerData.shared);
    Atomics.add(counter, 0, 42);
    parentPort.postMessage('done');
}
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

    socket.on('error', (err) => console.error(err.code));
});

server.listen(3000, () => {
    console.log('Server listening on port 3000');
});
```

### File Operations
```javascript
const fs = require('fs');

// Without an encoding you get a Buffer
const bytes = fs.readFileSync('logo.png');
const text = fs.readFileSync('notes.txt', 'utf8');

fs.readFile('missing.txt', 'utf8', (err, data) => {
    if (err) return console.error(err.code);  // 'ENOENT'
    console.log(data);
});

// Promises and streams
await fs.promises.writeFile('output.txt', 'Hello, FrogJS!');
fs.createReadStream('big.log').pipe(fs.createWriteStream('copy.log'));
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

### TypeScript
`types/` holds declarations for the FrogJS globals and built-in modules. They describe FrogJS's actual behavior where it differs from Node.js. Compile to CommonJS and run the output:

```typescript
// tsconfig.json: { "compilerOptions": { "module": "CommonJS", "lib": ["ES2022"], "types": [], "strict": true },
//                  "include": ["src", "path/to/frogjs/types/index.d.ts"] }
import fs = require('fs');
import { execSync } from 'child_process';

const branch: string = execSync('git branch --show-current', { encoding: 'utf8' }).trim();
fs.writeFileSync('branch.txt', branch);
```

See [types/README.md](types/README.md).

---

## Examples

Check out the `examples/` directory for more samples:
- `examples/timers.js` - Timer API usage
- `examples/fs-sync.js` - Synchronous file operations
- `examples/fs-async.js` - Asynchronous file operations
- `examples/http-server.js` - HTTP server with basic routing
- `examples/http-client.js` - HTTP client (GET and POST)
- `examples/test-http.js` - HTTP module self-test
- `examples/events-streams.js` - EventEmitter, Transform and HTTP streaming demo
- `examples/test-events.js` - Events module self-test
- `examples/test-streams.js` - Stream module self-test
- `examples/tcp-server.js` - TCP server implementation
- `examples/tcp-client.js` - TCP client implementation
- `examples/modules/` - Module system examples
- `examples/test-modules.js` - Module system self-test
- `examples/test-buffer.js` - Buffer self-test
- `examples/test-path.js` - Path module self-test
- `examples/test-os.js` - OS module self-test
- `examples/test-fs.js` - File system self-test
- `examples/test-net.js` - TCP self-test
- `examples/test-http-limits.js` - HTTP against hostile input
- `examples/test-process.js` - Process and globals self-test
- `examples/test-timers.js` - Timers self-test
- `examples/test-module-loading.js` - Module loader self-test
- `examples/test-errors.js` - Uncaught errors, rejections and exit codes
- `examples/test-edges.js` - Argument errors in events, crypto and workers
- `examples/test-memory.js` - Long-running work doesn't grow the process
- `examples/test-util.js` - Util module self-test (`inspect`, `format`)
- `examples/test-crypto.js` - Crypto module and Buffer encodings self-test
- `examples/test-child-process.js` - Child process module self-test
- `examples/test-worker-threads.js` - Worker threads self-test
- `examples/test-cli.sh` - CLI self-test
- `examples/test-repl.py` - REPL self-test on a real terminal

Run examples:
```bash
./build/frogjs examples/timers.js
./build/frogjs examples/http-server.js
./build/frogjs examples/tcp-server.js
./build/frogjs examples/modules/main.js
```

---

## Project Structure

```
frogjs/
├── src/
│   ├── core/
│   │   ├── runtime.cpp       # Main entry point, command-line options, console
│   │   ├── repl.cpp          # Interactive REPL
│   │   └── line_editor.cpp   # Line editing, history and completion for the REPL
│   └── bindings/
│       ├── process.cpp      # process object
│       ├── buffer.cpp       # Buffer class
│       ├── timers.cpp       # Timer API
│       ├── fs.cpp           # File system API
│       ├── net.cpp          # TCP networking
│       ├── events.cpp       # EventEmitter (embedded JS)
│       ├── stream.cpp       # Readable/Writable/Duplex/Transform (embedded JS)
│       ├── http.cpp         # HTTP server & client (JS on top of net)
│       ├── path.cpp         # Path utilities
│       ├── os.cpp           # OS information
│       ├── util.cpp         # util.inspect / util.format (embedded JS)
│       ├── crypto.cpp       # crypto module (embedded JS on native helpers)
│       ├── child_process.cpp # spawn/exec/execSync (uv_spawn, pipes as streams)
│       ├── worker_threads.cpp # Workers (isolate + loop per thread), MessagePort
│       └── modules.cpp      # Module system
│   └── utils/
│       ├── js_module.cpp    # Loader for modules written in embedded JS
│       ├── errors.cpp       # Node-style uncaught error reports
│       ├── encoding.cpp     # hex/base64/latin1/... for Buffer and crypto
│       └── hash.cpp         # MD5, SHA-1, SHA-2, HMAC, PBKDF2
├── examples/                # Example scripts and self-tests (test-*.js)
├── docs/                    # API reference, architecture, contributing and style guides
├── include/frogjs/          # Shared headers (runtime, loop, workers, V8 compat, Buffer, ...)
├── types/                   # TypeScript declarations (index.d.ts) and their tests
├── tools/                   # Development scripts (check-types.js)
├── Makefile                 # Build, test, lint, format and type-check targets
├── package.json             # Dev tools: ESLint and TypeScript
├── eslint.config.js         # ESLint, including the JavaScript embedded in src/
├── .clang-format            # C++ formatting
├── CMakeLists.txt          # CMake build (in development)
├── README.md               # This file
├── FEATURES.md             # Detailed feature list
└── note.md                 # Development notes
```

---

## Current Status

**1.0.0**: see the [changelog](CHANGELOG.md), and the [API stability](docs/api-stability.md) and [support](docs/support-policy.md) policies.

### Implemented
- Core runtime with V8 integration; uncaught errors and unhandled rejections end the process with a report, as in Node.js
- Console API (log, info, debug, error, warn) with `util.format` formatting
- Timers: setTimeout, setInterval, setImmediate, process.nextTick, queueMicrotask, with ref/unref
- File system: sync, callback and promise forms; read and write streams; descriptors; Error objects with `code`
- TCP: Duplex sockets with backpressure, EventEmitter servers, DNS, timeouts
- HTTP server/client: keep-alive, chunked encoding, pipelining, backpressure and request timeouts
- Events (EventEmitter, events.once, events.on) and streams (Readable, Writable, Duplex, Transform, PassThrough, pipeline, finished)
- Module system: node_modules, package.json (main, exports), JSON, circular requires, require.resolve/cache/main
- REPL (history, tab completion, multi-line input, commands) and CLI options
- Util (inspect, format, promisify, inherits, isDeepStrictEqual)
- Crypto (createHash, createHmac, pbkdf2, randomBytes, randomUUID, randomInt, getRandomValues, timingSafeEqual)
- Child processes (spawn, exec, execFile, spawnSync, execSync, execFileSync)
- Worker threads (Worker, parentPort, workerData, MessageChannel, SharedArrayBuffer), structuredClone
- Process: EventEmitter with 'exit', 'uncaughtException', 'unhandledRejection' and signals; stdin/stdout/stderr streams; hrtime, memoryUsage, kill, ...
- Buffer: a Uint8Array subclass with encodings and binary read/write methods
- Path and OS modules; TextEncoder/TextDecoder; performance.now
- TypeScript declarations for every built-in module (`types/`), checked against the runtime
- Tooling: `make check`, `test-asan`, `leakcheck`, `coverage`, `bench`, `fuzz`

### Not included
ES modules, TLS/HTTPS, ciphers and signatures, `child_process.fork()` with IPC, a global `URL`, `fetch`, Windows. Possible later versions are in the [roadmap](ROADMAP.md).

## Documentation

- **[FEATURES.md](FEATURES.md)** - Comprehensive feature documentation
- **[docs/api-reference.md](docs/api-reference.md)** - API reference
- **[CHANGELOG.md](CHANGELOG.md)** - What changed, and how to migrate to 1.0
- **[docs/api-stability.md](docs/api-stability.md)** - What is stable and what can change
- **[docs/support-policy.md](docs/support-policy.md)** - Supported versions and platforms
- **[SECURITY.md](SECURITY.md)** - Reporting vulnerabilities, threat model
- **[docs/security-audit.md](docs/security-audit.md)** - The 1.0 security review
- **[docs/performance.md](docs/performance.md)** - Benchmarks against Node.js
- **[docs/architecture.md](docs/architecture.md)** - How the runtime works
- **[docs/contributing.md](docs/contributing.md)** - Contributing guide: setup, checks, tests
- **[docs/style-guide.md](docs/style-guide.md)** - Formatting and coding conventions
- **[types/README.md](types/README.md)** - Using the TypeScript declarations
- **[note.md](note.md)** - Development guide and implementation notes
- **[steps.md](steps.md)** - Step-by-step implementation guide
- **[RELEASE_GUIDE.md](RELEASE_GUIDE.md)** - How to cut a release

---

## Building from Source

### Makefile Build (Recommended for macOS)
```bash
make clean
make
./build/frogjs your-script.js
```

`make DEBUG=1` builds without optimization for debugging. `CMakeLists.txt` is outdated and not maintained: use the Makefile.

---

## Performance

Measured against Node.js 24 on the same machine with `make bench`: startup is about 15 ms (Node.js: 20 ms), TCP throughput and file I/O match or beat Node.js, and HTTP is within a factor of 2. 4 of 23 benchmarks are 2 to 3 times slower. All the numbers are in [docs/performance.md](docs/performance.md).

---

## Releasing

Releases are automated with GitHub Actions ([`.github/workflows/release.yml`](.github/workflows/release.yml)).
Each release builds FrogJS on macOS, runs a smoke test, and publishes a GitHub Release with the
`.tar.gz` binary, a SHA-256 checksum, and notes generated from the commits since the previous tag.

Start a release in either of these ways:

```bash
# 1. Push a tag (via the helper script or directly)
./release.sh v0.5.0
git tag -a v0.5.0 -m "Release v0.5.0" && git push origin v0.5.0

# 2. Trigger from GitHub (the workflow creates the tag for you)
gh workflow run release.yml -f version=v0.5.0
#    or: Actions → Release → Run workflow
```

Versions with a suffix such as `v0.5.0-rc.1` are published as pre-releases.
CI for pushes and pull requests runs separately in [`.github/workflows/build.yml`](.github/workflows/build.yml).
See [RELEASE_GUIDE.md](RELEASE_GUIDE.md) for details.

---

## Contributing

Contributions are welcome! See the [contributing guide](docs/contributing.md) and the [style guide](docs/style-guide.md).

### Development Setup
```bash
make            # build
npm install     # ESLint and TypeScript
make check      # lint + tests + type checks (what CI runs)
make format     # clang-format the C++ you changed
```

---

## License

MIT License - See LICENSE file for details

---

## Acknowledgments

- **V8 Team** - For the amazing JavaScript engine
- **libuv Team** - For the excellent I/O library
- **Node.js** - For architectural inspiration

---

**Version**: 1.0.0 — see [Releases](https://github.com/dev-frog/frogJs/releases) for the latest
**Status**: Stable (macOS on Apple Silicon)
**Last Updated**: 2026-11-09

## Support 🐸

If you enjoy using FrogJS, consider giving it a ⭐ on GitHub! For questions or feedback, open an issue or reach out to us.
