# FrogJS API Reference

Complete API documentation for FrogJS runtime and built-in modules. `make check-docs` verifies that every declared export appears here. What is stable and what can change is in the [API stability policy](api-stability.md).

## Table of Contents
- [Command Line](#command-line)
- [REPL](#repl)
- [Global Objects](#global-objects)
- [Process API](#process-api)
- [Console API](#console-api)
- [Util API](#util-api)
- [Buffer API](#buffer-api)
- [Buffer Encodings](#buffer-encodings)
- [Crypto API](#crypto-api)
- [Child Process API](#child-process-api)
- [Worker Threads API](#worker-threads-api)
- [Timer API](#timer-api)
- [File System API](#file-system-api)
- [Path API](#path-api)
- [OS API](#os-api)
- [Network API](#network-api)
- [HTTP API](#http-api)
- [Events API](#events-api)
- [Stream API](#stream-api)
- [Module System](#module-system)

---

## Command Line

```
frogjs [options] [ script.js ] [arguments]
frogjs [options] -e "script" [arguments]
frogjs                                     # start the REPL
```

| Option | Description |
|--------|-------------|
| `-e`, `--eval <script>` | Evaluate `script`. `__filename` is `[eval]`; arguments after it go to `process.argv` |
| `-p`, `--print <script>` | Evaluate `script` and print the result like `console.log` would |
| `-c`, `--check` | Compile the script (or stdin) without running it; exit code 1 on a syntax error |
| `-i`, `--interactive` | Start the REPL even if stdin is not a terminal, after running the script or `--eval` code |
| `-v`, `--version` | Print the version (`v1.0.0`) |
| `-h`, `--help` | Print usage |
| `-` | Read the script from stdin (the default when no script is given and stdin is not a terminal) |
| `--` | End of options: the next argument is the script |

Short options combine (`-pe "1 + 1"`), and long options accept `--eval=code`. Options come before the script; everything after the script name is passed to it.

**`process.argv`** is `[executable, script, ...arguments]` (no script entry for `--eval` and the REPL); **`process.execArgv`** holds the FrogJS options.

**Exit codes:**
- `0` - success
- `1` - uncaught error in the main script, or the script file can't be read
- `9` - invalid option

**Uncaught errors** print the location, the source line, a caret and the stack; the event loop does not run afterwards:
```
app.js:3
  return user.name;
              ^

TypeError: Cannot read properties of undefined (reading 'name')
    at getName (app.js:3:15)
    at app.js:6:1
```
Errors thrown by timer and `fs` callbacks are reported the same way, but the process keeps running.

**Environment variables:**
- `FROGJS_REPL_HISTORY` - REPL history file (default `~/.frogjs_repl_history`); an empty value disables history
- `NO_COLOR` - disable colors in REPL output

---

## REPL

Start it with `frogjs` (no script) in a terminal, or with `frogjs -i`.

```
> [1, 2, 3].map(n => n * 2)
[ 2, 4, 6 ]
> function add(a, b) {
... return a + b }
undefined
> add(2, 3)
5
> _ * 10
50
```

- Results are printed with `util.inspect` (colored in a terminal)
- Input that is not finished yet (an open `{`, `(` or template literal) continues on a `...` prompt
- Input that starts with `{` is tried as an object literal first: `{ a: 1 }` prints `{ a: 1 }`
- `_` holds the last result, `_error` the last uncaught error
- The event loop runs while the prompt waits: timers fire and servers accept connections, and their output is printed above the prompt
- Without a terminal (`echo "1 + 1" | frogjs -i`), lines are read and evaluated in order without line editing; the REPL ends at the end of input

| Command | Description |
|---------|-------------|
| `.help` | List the commands |
| `.exit` | Exit the REPL |
| `.clear` | Abandon the current input and start over in a fresh context (globals, declarations and the module cache are cleared) |
| `.break` | Abandon multi-line input |
| `.load <file>` | Evaluate a file in the session |
| `.save <file>` | Save the lines entered so far to a file |

| Key | Action |
|-----|--------|
| Enter | Evaluate the line |
| Tab | Complete globals, keywords, declared names, `a.b.` properties and commands; press again to list candidates |
| Up / Down, Ctrl+P / Ctrl+N | Browse history (saved between sessions, 1000 lines) |
| Left / Right, Ctrl+B / Ctrl+F, Alt+B / Alt+F | Move by character / by word |
| Home / End, Ctrl+A / Ctrl+E | Start / end of line |
| Backspace, Delete, Ctrl+D | Delete a character |
| Ctrl+K / Ctrl+U / Ctrl+W | Delete to end of line / to start of line / previous word |
| Ctrl+L | Clear the screen |
| Ctrl+C | Clear the input, or interrupt running code; twice on an empty line exits |
| Ctrl+D | Exit (on an empty line) |

---

## Global Objects

Every built-in module is also a global of the same name (`fs`, `http`, `child_process`, ...), the object `require()` returns. Besides those and the [standard JavaScript globals](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects):

### `__filename`, `__dirname`
The absolute path of the current module and its directory. In `-e` code, stdin and the REPL, `__filename` is `[eval]`, `[stdin]` or `[repl]` and `__dirname` is `.`.

### `require(id)`, `module`, `exports`
See [Module System](#module-system).

### `global`
The global object (the same as `globalThis`).

### `setTimeout`, `setInterval`, `setImmediate`, `clearTimeout`, `clearInterval`, `clearImmediate`
See [Timer API](#timer-api).

### `queueMicrotask(callback)`
Run `callback` as a microtask. An exception from it is an uncaught exception.

### `structuredClone(value[, { transfer }])`
A deep copy by the structured clone algorithm: `Map`, `Set`, `Date`, `RegExp`, `BigInt`, errors, typed arrays and cycles work; functions throw `DataCloneError`. `transfer` lists `ArrayBuffer`s and `MessagePort`s to move instead of copy.

### `performance`
`performance.now()` is milliseconds since the process started, with sub-millisecond precision. `performance.timeOrigin` is when that was, in Unix milliseconds. `performance.toJSON()` returns `{ timeOrigin }`.

### `TextEncoder`, `TextDecoder`
`new TextEncoder().encode(string)` returns a `Uint8Array` of UTF-8; `encoding` is `'utf-8'`. `new TextDecoder([encoding[, { fatal, ignoreBOM }]]).decode(bytes)` supports UTF-8 only (`ERR_ENCODING_NOT_SUPPORTED` otherwise); with `fatal`, invalid input throws `ERR_ENCODING_INVALID_ENCODED_DATA`.

---

## Process API

`process` is a global and an [EventEmitter](#events-api); also `require('process')`.

### Properties
| Property | Meaning |
|----------|---------|
| `process.argv` | `[execPath, absolute script path, ...arguments]` (no script entry for `-e` and the REPL) |
| `process.execArgv` | FrogJS options before the script name |
| `process.execPath` | Absolute path of the executable |
| `process.env` | Environment variables, as a plain object. Child processes get its current content |
| `process.pid`, `process.ppid` | Process IDs |
| `process.platform`, `process.arch` | e.g. `'darwin'`, `'arm64'` |
| `process.version`, `process.versions` | `'v1.0.0'`; `{ frogjs, v8, uv }` |
| `process.release` | `{ name: 'frogjs' }` |
| `process.title` | `'frogjs'` (setting it only changes the property) |
| `process.config`, `process.features` | Empty objects, for code that reads them |
| `process.exitCode` | The exit code when the process ends by itself or `exit()` gets no code |

### `process.exit([code])`
Emit `'exit'` once and end the process (on a worker thread: only the worker). Nothing after it runs.

### `process.cwd()`, `process.chdir(directory)`, `process.umask([mask])`
Current directory (failures throw with `code`), and the file mode mask: reading returns it, setting returns the previous one.

### `process.nextTick(callback[, ...args])`
Run `callback` after the current operation, before I/O and timers. Ticks share the microtask queue with promises in FrogJS, so a promise reaction queued *before* the tick runs first (in Node.js ticks always go first).

### `process.hrtime([previous])`, `process.hrtime.bigint()`, `process.uptime()`
A monotonic clock: `[seconds, nanoseconds]`, or the time since `previous`; nanoseconds as a BigInt; seconds since the process started.

### `process.memoryUsage()`, `process.memoryUsage.rss()`, `process.cpuUsage()`
`{ rss, heapTotal, heapUsed, external }` in bytes; `{ user, system }` in microseconds.

### `process.kill(pid[, signal])`
Send a signal (a name, or a number; default `'SIGTERM'`; `0` tests whether the process exists). Failure throws with `code`, such as `ESRCH`.

### `process.emitWarning(warning[, type[, code]])`
Emit `'warning'` on the next tick. Printed to stderr if nothing listens.

### `process.stdout`, `process.stderr`
Writable streams for file descriptors 1 and 2 (`write()`, `fd`, `isTTY`). Writes are synchronous and ordered with `console.log`; `end()` doesn't close the descriptor, so `child.stdout.pipe(process.stdout)` is safe.

### `process.stdin`
A Readable over file descriptor 0, created on first use (`isTTY` tells whether it is a terminal). Reading keeps the process alive until the end of the input.

### Events
| Event | Arguments | When |
|-------|-----------|------|
| `'exit'` | `code` | The process is ending. Only synchronous work runs. A listener can change `process.exitCode` |
| `'beforeExit'` | `code` | The event loop ran out of work (not on `process.exit()`). A listener can schedule more |
| `'uncaughtException'` | `err`, `origin` | An exception nobody caught. With a listener the process goes on |
| `'unhandledRejection'` | `reason`, `promise` | A rejected promise without a handler after the microtasks ran. Without a listener it becomes an uncaught exception |
| `'warning'` | `warning` | `process.emitWarning()` |
| `'SIGINT'`, `'SIGTERM'`, ... | `name`, `number` | The signal arrived (main thread only). A listener replaces the default action; it doesn't keep the process alive by itself |

---

## Console API

### `console.log(...args)`
Prints to stdout with newline.

```javascript
console.log('Hello, World!');
console.log('Value:', 42, 'Object:', { key: 'value' });
```

### `console.error(...args)`
Prints to stderr with newline.

```javascript
console.error('Error occurred');
console.error('Code:', 500, 'Message:', 'Internal Server Error');
```

### `console.info()`, `console.debug()`, `console.warn()`
`info` and `debug` are aliases of `console.log`; `warn` is an alias of `console.error`.

### Formatting
All console methods format their arguments with [`util.format`](#utilformatformat-args): strings are printed as-is, other values with `util.inspect`, and a first string argument may contain format specifiers.

```javascript
console.log({ a: 1, b: [1, 2] });       // { a: 1, b: [ 1, 2 ] }
console.log('%s is %d years', 'Bob', 42); // Bob is 42 years
```

---

## Util API

`require('util')` (also the global `util`).

### `util.inspect(value[, options])`
Returns a string representation of `value` meant for debugging. The output matches Node.js.

```javascript
util.inspect({ a: { b: { c: { d: 1 } } } }); // '{ a: { b: { c: [Object] } } }'
util.inspect(new Map([['k', [1, 2]]]));      // "Map(1) { 'k' => [ 1, 2 ] }"
const obj = { name: 'loop' }; obj.self = obj;
util.inspect(obj);                          // "<ref *1> { name: 'loop', self: [Circular *1] }"
```

**Options:**
- `depth` (Number, default `2`): how deep to recurse into objects; `null` for no limit
- `colors` (Boolean, default `false`): add ANSI colors
- `showHidden` (Boolean, default `false`): include non-enumerable properties
- `maxArrayLength` (Number, default `100`) and `maxStringLength` (Number, default `10000`)
- `breakLength` (Number, default `80`): line length at which entries are split over lines
- `compact` (Number or Boolean, default `3`) and `sorted` (Boolean or comparator)

Shows class names (`Point { x: 1 }`), getters (`[Getter]`), functions (`[Function: name]`, `[class A extends B]`), `Map`, `Set`, typed arrays, `ArrayBuffer`, `Promise` state, `Proxy` targets, errors (stack plus extra properties), `Buffer` (`<Buffer 68 69>`), boxed primitives, dates and regular expressions.

An object can define its own representation:
```javascript
class Money {
    constructor(cents) { this.cents = cents; }
    [util.inspect.custom]() { return `$${(this.cents / 100).toFixed(2)}`; }
}
util.inspect(new Money(1999)); // '$19.99'
```

### `util.format(format, ...args)`
Like `printf`. Supported specifiers: `%s` (string), `%d` (number), `%i` (integer), `%f` (float), `%j` (JSON), `%o` (`inspect` with hidden properties), `%O` (`inspect`), `%c` (ignored CSS), `%%` (a literal `%`). Extra arguments are appended, separated by spaces.

```javascript
util.format('%s:%d', 'port', 8080);   // 'port:8080'
util.format('%j', { a: 1 });          // '{"a":1}'
util.format('a', { b: 1 }, [2]);      // 'a { b: 1 } [ 2 ]'
```

### `util.formatWithOptions(inspectOptions, format, ...args)`
`util.format` with options passed to `inspect`.

### `util.promisify(fn)`
Wraps a function taking an `(err, value)` callback as its last argument into one returning a Promise.

```javascript
const sleep = util.promisify((ms, cb) => setTimeout(() => cb(null, ms), ms));
sleep(100).then((ms) => console.log(`waited ${ms}ms`));
```

### `util.inherits(constructor, superConstructor)`
Sets up prototype inheritance between constructor functions (prefer `class ... extends`).

### `util.isDeepStrictEqual(a, b)`
`true` if `a` and `b` have the same own enumerable properties and prototypes, compared recursively.

### `util.stripVTControlCharacters(str)`
Removes ANSI color codes from `str`.

---

## Timer API

### `setTimeout(callback[, delay[, ...args]])`, `setInterval(callback[, delay[, ...args]])`
Run `callback` once after `delay` milliseconds, or repeatedly. Delays outside 1 to 2^31-1 become 1. Extra arguments go to the callback; `this` is the timer. Returns a `Timeout`:

| Method | |
|--------|---|
| `ref()`, `unref()`, `hasRef()` | Whether this timer keeps the process alive (by default it does) |
| `refresh()` | Start the delay over; a timeout that already ran runs again |
| `close()` | Like `clearTimeout()` |
| `[Symbol.toPrimitive]()` | Its numeric ID, also accepted by `clearTimeout()` |

A callback that isn't a function throws `ERR_INVALID_ARG_TYPE`.

### `setImmediate(callback[, ...args])`
Run `callback` after the I/O of the current loop iteration. An immediate added while immediates run waits for the next iteration. Returns an `Immediate` with `ref()`, `unref()` and `hasRef()`.

### `clearTimeout(timer)`, `clearInterval(timer)`, `clearImmediate(immediate)`
Cancel it. Anything else, or a timer that already ended, is ignored.

`require('timers')` has the same functions.

---

## File System API

`fs` follows Node.js. Every asynchronous function has a callback form, and most a `*Sync` form that throws instead (`mkdirSync`, ...). They behave the same: both run the same native code, the callback form on the thread pool. `fs.promises` has promise versions.

**Errors** are `Error` objects: `code` (`'ENOENT'`), `errno`, `syscall` (`'open'`), `path`, and `dest` for two-path calls; the message is like `ENOENT: no such file or directory, open 'x'`. Invalid arguments throw `TypeError` with `ERR_INVALID_ARG_TYPE` or `ERR_INVALID_ARG_VALUE`, even for callback forms. Paths with a null byte are rejected.

**Paths** are strings, Buffers or `Uint8Array`s (UTF-8).

### Whole files
| Function | |
|----------|---|
| `fs.readFile(path[, options], callback)` / `readFileSync` | `path` can be a file descriptor. Without an encoding the result is a Buffer. `options` is an encoding or `{ encoding, flag }` |
| `fs.writeFile(path, data[, options], callback)` / `writeFileSync` | `data` is a string or typed array. `options` is an encoding or `{ encoding, mode, flag }` |
| `fs.appendFile(path, data[, options], callback)` / `appendFileSync` | The same, appending |

```javascript
const text = fs.readFileSync('config.json', 'utf8');
const bytes = fs.readFileSync('logo.png');          // a Buffer
fs.writeFile('out.txt', text, (err) => { if (err) console.error(err.code); });
const data = await fs.promises.readFile('config.json', 'utf8');
```

Flags are `'r'`, `'r+'`, `'w'`, `'w+'`, `'wx'`, `'a'`, `'a+'`, `'ax'` and the rest of Node.js's strings, or a number.

### Directories
| Function | |
|----------|---|
| `fs.mkdir(path[, options], callback)` / `mkdirSync` | `options` is a mode or `{ mode, recursive }`. With `recursive`, the result is the first directory created (absolute), or `undefined` |
| `fs.readdir(path[, options], callback)` / `readdirSync` | Entry names without `.` and `..`. With `{ withFileTypes: true }`, `Dirent` objects: `name`, `parentPath`, `isFile()`, `isDirectory()`, `isSymbolicLink()` |
| `fs.rmdir(path[, options], callback)` / `rmdirSync` | Remove an empty directory |
| `fs.rm(path[, options], callback)` / `rmSync` | Remove a file or, with `{ recursive: true }`, a tree. `force` ignores a missing path. Symbolic links are removed, not followed |

### Metadata
| Function | |
|----------|---|
| `fs.stat(path[, options], callback)` / `statSync` | A `Stats`. `statSync(path, { throwIfNoEntry: false })` returns `undefined` if it doesn't exist |
| `fs.lstat(path[, options], callback)` / `lstatSync` | The same, about a symbolic link itself |
| `fs.fstat(fd[, options], callback)` / `fstatSync` | The same, for a descriptor |
| `fs.realpath(path[, options], callback)` / `realpathSync` | The canonical absolute path |
| `fs.access(path[, mode], callback)` / `accessSync` | Fails if `mode` (`fs.constants.R_OK`, ...) isn't allowed |
| `fs.existsSync(path)` | `true` or `false`, never throws |

`Stats` has `dev`, `ino`, `mode`, `nlink`, `uid`, `gid`, `rdev`, `size`, `blksize`, `blocks`, `atimeMs`, `mtimeMs`, `ctimeMs`, `birthtimeMs`, `atime`, `mtime`, `ctime`, `birthtime`, and the methods `isFile()`, `isDirectory()`, `isSymbolicLink()`, `isFIFO()`, `isSocket()`, `isBlockDevice()`, `isCharacterDevice()`.

### Moving and deleting
`fs.rename(oldPath, newPath, callback)` / `renameSync`, `fs.copyFile(src, dest[, mode], callback)` / `copyFileSync` (`mode`: `fs.constants.COPYFILE_EXCL` fails if `dest` exists), `fs.unlink(path, callback)` / `unlinkSync`.

### File descriptors
| Function | |
|----------|---|
| `fs.open(path[, flags[, mode]], callback)` / `openSync` | A descriptor |
| `fs.close(fd[, callback])` / `closeSync` | |
| `fs.read(fd, buffer[, offset[, length[, position]]], callback)` / `readSync` | Also `(fd, buffer, { offset, length, position })`. Gives the bytes read, 0 at the end of the file. A `position` of `null` reads from the current position |
| `fs.write(fd, buffer[, offset[, length[, position]]], callback)` / `writeSync` | Gives the bytes written, which can be fewer than given |
| `fs.write(fd, string[, position[, encoding]], callback)` / `writeSync` | |

`fs.constants` has `F_OK`, `R_OK`, `W_OK`, `X_OK`, `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND`, `O_EXCL`, `O_SYNC` and `COPYFILE_EXCL`.

### Streams
`fs.createReadStream(path[, options])` returns a `ReadStream`, a [Readable](#readable). Options: `encoding`, `start` and `end` (inclusive byte positions), `highWaterMark` (default 64 KB), `flags`, `mode`, `fd` (use this descriptor; `path` is ignored), `autoClose`, `emitClose`. Properties: `path`, `fd`, `flags`, `mode`, `start`, `end`, `autoClose`, `bytesRead`. Events: `'open'` (with the descriptor), `'ready'`, and those of a Readable. `close([callback])` destroys it.

`fs.createWriteStream(path[, options])` returns a `WriteStream`, a [Writable](#writable). Options: `flags` (default `'w'`), `encoding`, `start`, `mode`, `fd`, `autoClose`, `emitClose`, `highWaterMark`. Properties: `path`, `fd`, `flags`, `mode`, `start`, `autoClose`, `bytesWritten`. `close([callback])` ends it.

```javascript
fs.createReadStream('in.log').pipe(fs.createWriteStream('copy.log'));
```

### `fs.promises`
`readFile`, `writeFile`, `appendFile`, `mkdir`, `readdir`, `rmdir`, `rm`, `unlink`, `stat`, `lstat`, `realpath`, `rename`, `copyFile`, `access`: the same arguments without the callback.

---

## Path API

POSIX paths; `require('path')` and `path.posix` are the same object. Non-string arguments throw `ERR_INVALID_ARG_TYPE`.

| Function | |
|----------|---|
| `path.join(...paths)` | Join with `/` and normalize (`.` for nothing) |
| `path.resolve(...paths)` | Right to left, until the result is absolute; then from `process.cwd()` |
| `path.normalize(path)` | Collapse `.`, `..` and repeated slashes |
| `path.relative(from, to)` | The relative path between two paths |
| `path.dirname(path)`, `path.basename(path[, suffix])`, `path.extname(path)` | The parts of a path |
| `path.isAbsolute(path)` | |
| `path.parse(path)` | `{ root, dir, base, ext, name }` |
| `path.format(pathObject)` | The reverse of `parse` |
| `path.toNamespacedPath(path)` | Returns `path` (it only matters on Windows) |
| `path.sep`, `path.delimiter` | `'/'`, `':'` |
| `path.posix` | `path` itself |

---

## OS API

`os.platform()`, `os.arch()`, `os.type()`, `os.release()`, `os.hostname()`, `os.homedir()`, `os.tmpdir()`, `os.totalmem()`, `os.freemem()`, `os.EOL`, and `os.cpus()`, an array of `{ model, speed, times: { user, nice, sys, idle, irq } }`. The values match Node.js's on the same machine.

---

## Network API

`net` follows Node.js: sockets are [Duplex](#duplex) streams and servers are [EventEmitters](#events-api).

### `net.createServer([options][, connectionListener])`
A `Server`. `allowHalfOpen` keeps a connection writable after the peer has ended its side (call `socket.end()` when done). The listener is added for `'connection'`.

### Server
| Member | |
|--------|---|
| `server.listen(port[, host][, backlog][, callback])`, `server.listen({ port, host, backlog }[, callback])` | Start listening. Port 0 picks a free port; the host defaults to `'0.0.0.0'` (`'::'` in Node.js). Failures such as `EADDRINUSE` are `'error'` events. Calling it twice throws `ERR_SERVER_ALREADY_LISTEN` |
| `server.close([callback])` | Stop accepting. `'close'` follows when the existing connections have ended |
| `server.address()` | `{ address, family, port }`, or `null` when not listening |
| `server.getConnections(callback)` | `callback(null, count)` |
| `server.listening` | |
| `server.maxConnections` | Over this many, new connections are closed and `'drop'` is emitted |
| `server.ref()`, `server.unref()` | Whether it keeps the process alive |

Events: `'connection'` (socket), `'listening'`, `'close'`, `'error'`, `'drop'`.

### `net.connect(...)`, `net.createConnection(...)`
`net.connect(port[, host][, listener])` or `net.connect({ port, host, timeout, allowHalfOpen, highWaterMark }[, listener])`. Returns a `Socket` that is connecting. The host defaults to `'localhost'`; names are resolved and each address tried, IPv4 first. A host with a null byte throws `ERR_INVALID_ARG_VALUE`.

### `net.Socket`
A Duplex stream: `pipe()`, `setEncoding()`, `pause()`, `for await`, and `write()` returning `false` when the kernel can't keep up, followed by `'drain'`. Received data is a Buffer until `setEncoding()` is called.

| Member | |
|--------|---|
| `new net.Socket([options])`, `socket.connect(...)` | Same arguments as `net.connect` |
| `socket.write(data[, encoding][, callback])`, `socket.end([data][, encoding][, callback])` | From Writable. `end()` sends FIN after pending writes |
| `socket.destroy([err])` | Close at once, discarding pending writes |
| `socket.destroySoon()` | End after pending writes, then destroy |
| `socket.setTimeout(ms[, callback])`, `socket.timeout` | Emit `'timeout'` after `ms` of inactivity. The socket stays open: close it yourself |
| `socket.setNoDelay([enable])`, `socket.setKeepAlive([enable[, delay]])` | TCP options |
| `socket.address()` | The local `{ address, family, port }`, or `{}` |
| `socket.localAddress`, `socket.localPort`, `socket.localFamily` | |
| `socket.remoteAddress`, `socket.remotePort`, `socket.remoteFamily` | |
| `socket.bytesRead`, `socket.bytesWritten` | |
| `socket.connecting`, `socket.pending`, `socket.readyState` | `readyState` is `'opening'`, `'open'`, `'readOnly'`, `'writeOnly'` or `'closed'` |
| `socket.ref()`, `socket.unref()` | Whether it keeps the process alive |

Events: `'connect'`, `'ready'`, `'data'`, `'end'`, `'drain'`, `'timeout'`, `'error'`, and `'close'` with `hadError`. An `'error'` without a listener is an uncaught exception.

Errors have `code`, `errno` and `syscall`, plus `address` and `port` (`connect ECONNREFUSED 127.0.0.1:80`) or `hostname` for failed lookups.

```javascript
const server = net.createServer((socket) => socket.pipe(socket));
server.listen(0, '127.0.0.1', () => {
  const client = net.connect(server.address().port, '127.0.0.1');
  client.setEncoding('utf8');
  client.on('data', (text) => { console.log(text); client.end(); server.close(); });
  client.write('ribbit');
});
```

### `net.isIP(input)`, `net.isIPv4(input)`, `net.isIPv6(input)`
4, 6, or 0 if it isn't an IP address; booleans.

---

## HTTP API

HTTP/1.1 server and client. Available as the global `http` or via `require('http')`.

### `http.createServer([options][, requestListener])`
Create an HTTP server.

**Parameters:**
- `options` (Object): Optional. `maxHeaderSize` (Number, default 16384 bytes)
- `requestListener` (Function): `(req, res) => {}`, added as a `'request'` listener

**Returns:** `Server`

```javascript
const http = require('http');
const server = http.createServer((req, res) => {
    res.end('Hello!');
});
server.listen(3000);
```

### Server

#### `server.listen([port][, host][, callback])`
Start listening. `port` 0 (the default) picks a free port. `host` defaults to `0.0.0.0`. Also accepts `listen({ port, host }, callback)`. Listen errors (e.g. `EADDRINUSE`) are emitted as `'error'`.

#### `server.close([callback])`
Stop accepting connections. Idle keep-alive connections close immediately; busy ones close after their current response. `'close'` (and `callback`) fire once every connection has closed.

#### `server.address()`
Returns `{ address, family, port }`, or `null` when not listening.

#### `server.closeIdleConnections()` / `server.closeAllConnections()`
Close idle keep-alive connections, or every connection immediately.

#### Properties
- `server.listening` (Boolean)
- `server.keepAliveTimeout` (Number): Idle time before a keep-alive connection is closed (default 5000 ms)
- `server.headersTimeout` (Number): Time allowed to receive request headers (default 60000 ms)
- `server.requestTimeout` (Number): Time allowed to receive a whole request, body included, after its headers; the client gets a `408` (default 300000 ms, 0 turns it off)

#### Events
- `'request'` `(req, res)`: A request's headers have arrived
- `'listening'`: The server is bound
- `'connection'` `(socket)`: New TCP connection
- `'close'`: The server and all its connections have closed
- `'error'` `(err)`: Listen failure
- `'clientError'` `(err, socket)`: Malformed request or socket error. Without a listener, FrogJS replies `400` (`431` for oversized headers) and closes the connection
- `'checkContinue'` `(req, res)`: A request sent `Expect: 100-continue`. Call `res.writeContinue()` to accept it. Without a listener, the `100 Continue` is sent automatically

If a request handler (or a `req`/`res` listener) throws, the error is printed and the client gets `500 Internal Server Error`.

### IncomingMessage
The `req` passed to a server's request handler, and the `res` passed to a client's response callback. It is a [`stream.Readable`](#readable), so it can be piped (`req.pipe(res)`), passed to `pipeline()` or read with `for await...of`.

- `method` (String), `url` (String): Request only
- `statusCode` (Number), `statusMessage` (String): Response only
- `headers` (Object): Header names lower-cased. Repeated headers are joined with `', '`; `set-cookie` is always an array
- `rawHeaders` (Array): `[name, value, name, value, ...]` as received
- `trailers` (Object): Trailers of a chunked message, available after `'end'`
- `httpVersion` (String): e.g. `'1.1'`
- `socket`: The underlying TCP socket (`socket.remoteAddress`, `socket.remotePort`)
- `complete` (Boolean): The whole message has been received

**Events:**
- `'data'` `(chunk)`: Body data as a UTF-8 string (multi-byte characters are never split between chunks)
- `'end'`: The body is complete
- `'aborted'`: The connection closed before the message completed
- `'close'`

Body data is buffered until a `'data'` listener is attached (or the message is piped), so the body can be read later. If the body is never read, `'end'` waits; call `resume()` to discard it. A message with no body emits `'end'` even if it is never read.

`message.destroy()` closes the connection if the body is still arriving.

#### `message.setEncoding(encoding)`
`'utf8'` (default) or `'latin1'`/`'binary'` to receive each byte as one character.

#### `message.resume()`
Let the body flow without a `'data'` listener (it is discarded).

```javascript
http.createServer((req, res) => {
    let body = '';
    req.on('data', (chunk) => body += chunk);
    req.on('end', () => {
        res.end(`Got ${req.method} ${req.url} with ${body.length} characters`);
    });
}).listen(3000);
```

### ServerResponse
`ServerResponse` and `ClientRequest` extend `http.OutgoingMessage`. Their `write()` returns `false` when the socket is full, followed by `'drain'`, like any Writable.


#### `res.statusCode` / `res.statusMessage`
Status sent with the headers (default `200` and the standard reason phrase).

#### `res.setHeader(name, value)` / `res.getHeader(name)` / `res.getHeaders()` / `res.getHeaderNames()` / `res.hasHeader(name)` / `res.removeHeader(name)`
Manage response headers before they are sent. `value` may be an array to send the header more than once. Throws on invalid names or values (CR/LF are rejected).

#### `res.writeHead(statusCode[, statusMessage][, headers])`
Set the status and merge `headers` (an object). After this, headers can no longer be changed.

#### `res.write(chunk[, encoding][, callback])`
Send part of the body (a string or Buffer). If no `Content-Length` header was set, the response uses chunked encoding (HTTP/1.1) or is delimited by closing the connection (HTTP/1.0).

#### `res.end([chunk][, encoding][, callback])`
Finish the response. If nothing was written before, `Content-Length` is set from `chunk`.

#### `res.writeContinue()`
Send `100 Continue`.

#### `res.destroy([error])`
Close the connection without finishing the response.

#### Properties and events
- `res.headersSent`, `res.finished` (Boolean)
- `res.sendDate` (Boolean): Add a `Date` header (default `true`)
- `'finish'`: The response has been handed to the socket
- `'close'`

`Connection` (`keep-alive` or `close`) and `Date` headers are added automatically. Responses to `HEAD` and `204`/`304` responses have no body.

A `ServerResponse` can be the destination of `pipe()` and `pipeline()`. Its `write()` always returns `true` (TCP sockets don't report backpressure yet), so a fast source is never paused.

```javascript
http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.write('streaming ');
    res.end('done');
}).listen(3000);
```

### `http.request(url[, options][, callback])` / `http.request(options[, callback])`
Make an HTTP request. The connection is opened immediately, but nothing is sent until `req.write()`/`req.end()`.

**Parameters:**
- `url` (String): An `http://` URL. HTTPS is not supported
- `options` (Object):
  - `hostname` / `host` (String): Default `'localhost'`. Hostnames are resolved via DNS
  - `port` (Number): Default `80`
  - `path` (String): Path and query string, default `'/'`
  - `method` (String): Default `'GET'`
  - `headers` (Object): Request headers
  - `timeout` (Number): Idle socket timeout in ms (emits `'timeout'`)
- `callback` (Function): `(res) => {}`, added as a `'response'` listener

**Returns:** `ClientRequest`

The `Host` and `Connection: close` headers are added automatically. Each request uses its own connection.

```javascript
const req = http.request({
    hostname: 'localhost',
    port: 3000,
    path: '/api',
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
}, (res) => {
    let body = '';
    res.on('data', (chunk) => body += chunk);
    res.on('end', () => console.log(res.statusCode, body));
});

req.on('error', (err) => console.error('Request failed:', err.code));
req.end(JSON.stringify({ hello: 'world' }));
```

### `http.get(url[, options][, callback])`
Like `http.request()` with method `GET`, and calls `req.end()` for you.

```javascript
http.get('http://localhost:3000/', (res) => {
    res.on('data', (chunk) => console.log(chunk));
});
```

### ClientRequest
- `req.write(chunk[, encoding][, callback])`: Send part of the body (chunked encoding unless `Content-Length` is set)
- `req.end([chunk][, encoding][, callback])`: Finish the request
- `req.setHeader()`, `req.getHeader()`, `req.removeHeader()`, `req.hasHeader()`: Before the headers are sent
- `req.setTimeout(ms[, callback])`: Emit `'timeout'` after `ms` without socket activity. The request is not aborted; call `req.destroy()`
- `req.destroy([err])` / `req.abort()`: Close the connection

**Events:**
- `'response'` `(res)`: Response headers received
- `'information'` `(info)`: A 1xx interim response (e.g. `100 Continue`)
- `'error'` `(err)`: Connection or protocol error. `err.code` is e.g. `'ECONNREFUSED'`, `'EAI_NONAME'` (DNS) or `'ECONNRESET'` (`socket hang up`)
- `'timeout'`, `'finish'`, `'close'`

### `http.METHODS` / `http.STATUS_CODES`
List of supported request methods, and a map from status code to reason phrase (`http.STATUS_CODES[404] === 'Not Found'`).

---

## Events API

Node.js-compatible event emitter. `require('events')` returns the `EventEmitter` class (also available as the global `events`). Every HTTP object is an `EventEmitter`.

```javascript
const EventEmitter = require('events');

class Downloader extends EventEmitter {}
const d = new Downloader();
d.on('progress', (pct) => console.log(`${pct}%`));
d.emit('progress', 50);  // true: there was a listener
```

### Listening
#### `emitter.on(name, listener)` / `emitter.addListener(name, listener)`
Add `listener` to the end of the list for `name` (a string or symbol). Returns the emitter.

#### `emitter.once(name, listener)`
Like `on()`, but the listener is removed before its first call.

#### `emitter.prependListener(name, listener)` / `emitter.prependOnceListener(name, listener)`
Add the listener to the front of the list.

#### `emitter.off(name, listener)` / `emitter.removeListener(name, listener)`
Remove the most recently added copy of `listener` (also works for `once()` listeners).

#### `emitter.removeAllListeners([name])`
Remove every listener for `name`, or for all events.

### Emitting
#### `emitter.emit(name, ...args)`
Call each listener synchronously, in order, with `args` and `this` set to the emitter. Returns `true` if there were listeners. Listeners added or removed during an `emit()` take effect from the next one. An exception thrown by a listener propagates to the caller of `emit()`.

### Inspecting
- `emitter.listeners(name)`: Copy of the listeners (`once()` wrappers unwrapped)
- `emitter.rawListeners(name)`: Copy including `once()` wrappers
- `emitter.listenerCount(name[, listener])`
- `emitter.eventNames()`: Names (and symbols) that have listeners
- `emitter.setMaxListeners(n)` / `emitter.getMaxListeners()`: Above this count a `MaxListenersExceededWarning` is printed once per event (0 = unlimited)
- `EventEmitter.defaultMaxListeners`: Default limit (10)

### `events.getEventListeners(emitter, name)`
A copy of the listeners for `name`, like `emitter.listeners(name)`.

### Special events
- `'error'`: If nothing listens, `emit('error', err)` throws `err` (or an `ERR_UNHANDLED_ERROR` error for non-`Error` values)
- `EventEmitter.errorMonitor`: Listeners for this symbol see every `'error'` before the regular listeners, without counting as handlers
- `'newListener'` `(name, listener)`: Emitted before a listener is added
- `'removeListener'` `(name, listener)`: Emitted after a listener is removed

### Helpers
#### `events.once(emitter, name)`
A Promise that resolves with the arguments array of the next `name` event, or rejects if `'error'` is emitted first.

```javascript
const [port] = await EventEmitter.once(server, 'ready');
```

#### `events.on(emitter, name)`
An async iterator over the argument arrays of `name` events. An `'error'` event makes the loop throw; `break` removes the listeners.

```javascript
for await (const [chunk] of EventEmitter.on(source, 'chunk')) {
    if (chunk === 'stop') break;
}
```

#### Inheritance
`class X extends EventEmitter`, the older `EventEmitter.call(this)` pattern, and objects created with `Object.create(EventEmitter.prototype)` all work.

---

## Stream API

Node.js-style streams. `require('stream')` returns the module (also the global `stream`), with `Readable`, `Writable`, `Duplex`, `Transform`, `PassThrough`, `pipeline`, `finished` and `promises`. All streams are `EventEmitter`s.

Chunks are strings or Buffers, or any value except `null` in `objectMode`. Unlike Node.js, strings are not converted to Buffers: `_write()` receives them as strings, and their length counts characters (for `highWaterMark` and `read(n)`).

**Common options** (constructor argument):
- `highWaterMark` (Number): Buffer size at which backpressure starts. Default 16384 (16 objects in `objectMode`)
- `objectMode` (Boolean): Chunks are arbitrary values; each counts as 1
- `emitClose` (Boolean): Emit `'close'` after destroy. Default `true`
- `autoDestroy` (Boolean): Destroy the stream after `'end'`/`'finish'`. Default `true`
- `destroy` (Function): Implementation of `_destroy(err, callback)`

### Readable
```javascript
const { Readable } = require('stream');

let n = 0;
const counter = new Readable({
    read() {
        n++;
        this.push(n <= 3 ? `tick ${n}\n` : null);  // null ends the stream
    },
});
counter.on('data', (chunk) => console.log(String(chunk)));
counter.on('end', () => console.log('done'));
```

**Options:** `read` (implementation of `_read(size)`), `encoding` (same as `setEncoding()`), plus the common options.

#### Implementing: `_read(size)` and `readable.push(chunk[, encoding])`
`_read()` is called when the stream wants data; it should `push()` chunks, now or later. `push()` returns `false` once the buffer reaches `highWaterMark`; stop pushing until `_read()` is called again. `push(null)` signals the end.

#### `readable.read([size])`
In paused mode, take `size` units (or everything buffered) from the buffer, or `null` if not enough is available. Emits `'data'` with the chunk.

#### `readable.pause()` / `readable.resume()` / `readable.isPaused()`
Switch between flowing and paused mode. Adding a `'data'` listener (or piping) starts flowing mode; adding a `'readable'` listener switches to paused mode.

#### `readable.setEncoding(encoding)`
Emit strings decoded from Buffers: `'utf8'` (characters split across chunks are reassembled), `'latin1'`/`'binary'`, `'ascii'` or `'hex'`.

#### `readable.pipe(destination[, options])`
Write all data to `destination` and return it. When `destination.write()` returns `false`, the source pauses until `'drain'`. `destination.end()` is called at the end unless `options.end` is `false`. Errors are not forwarded: use `pipeline()` for that.

#### `readable.unpipe([destination])`
Stop piping to `destination` (or to all destinations) and pause.

#### `readable.unshift(chunk)`
Put a chunk back at the front of the buffer.

#### `readable.destroy([error])`
Destroy the stream, emitting `'error'` (if `error` is given) and `'close'`.

#### `readable[Symbol.asyncIterator]()`
```javascript
for await (const chunk of readable) { ... }
```
Leaving the loop early (`break`, `return`, `throw`) destroys the stream.

#### `Readable.from(iterable[, options])`
A readable (in `objectMode` by default) of the values of an array, iterable, async iterable or generator.

#### Properties
`readable` (Boolean), `readableFlowing` (`null`, `true` or `false`), `readableLength`, `readableHighWaterMark`, `readableEncoding`, `readableEnded`, `readableObjectMode`, `destroyed`, `closed`, `errored`

#### Events
- `'data'` `(chunk)`, `'end'`, `'readable'`, `'pause'`, `'resume'`
- `'error'` `(err)`, `'close'`

### Writable
```javascript
const { Writable } = require('stream');

const log = new Writable({
    write(chunk, encoding, callback) {
        console.log('got', String(chunk));
        callback();  // pass an Error to fail the write
    },
});
log.write('a');
log.end('b', () => console.log('all written'));
```

**Options:** `write` (implementation of `_write(chunk, encoding, callback)`), `writev` (`_writev(chunks, callback)`, which receives `{ chunk, encoding }` entries when several writes are queued), `final` (`_final(callback)`, called before `'finish'`), `defaultEncoding`, plus the common options.

#### `writable.write(chunk[, encoding][, callback])`
Queue a chunk. Returns `false` when the queue has reached `highWaterMark` (checked after a synchronous `_write()`, as in Node.js 22+): stop writing until `'drain'`. `callback(err)` runs once the chunk is handled. Writing after `end()` emits an `ERR_STREAM_WRITE_AFTER_END` error.

#### `writable.end([chunk][, encoding][, callback])`
Write a final chunk, then finish. `callback` runs on `'finish'` (or with an error).

#### `writable.cork()` / `writable.uncork()`
Buffer writes until `uncork()`, then pass them to `_writev()` in one call if it is implemented.

#### `writable.setDefaultEncoding(encoding)` / `writable.destroy([error])`

#### Properties
`writable` (Boolean), `writableLength`, `writableHighWaterMark`, `writableNeedDrain`, `writableEnded`, `writableFinished`, `writableCorked`, `writableObjectMode`, `destroyed`, `closed`, `errored`

#### Events
- `'drain'`: The queue emptied after `write()` returned `false`
- `'finish'`: `end()` was called and all data was handled
- `'pipe'` `(src)`, `'unpipe'` `(src)`, `'error'` `(err)`, `'close'`

### Duplex
Both readable and writable, with independent sides: implement `_read()` and `_write()`. Options can be set per side with `readableHighWaterMark`/`writableHighWaterMark` and `readableObjectMode`/`writableObjectMode`. With `allowHalfOpen: false`, the writable side ends when the readable side ends. A `Duplex` is `instanceof` both `Readable` and `Writable`.

### Transform
A subclass implements `_transform(chunk, encoding, callback)` and optionally `_flush(callback)` (or passes them as options). `crypto.Hash` and `crypto.Hmac` are Transform streams with their own `_transform`.

A Duplex whose output is computed from its input.

```javascript
const { Transform } = require('stream');

const upper = new Transform({
    transform(chunk, encoding, callback) {
        callback(null, String(chunk).toUpperCase());  // or this.push(...) any number of times
    },
    flush(callback) {
        callback(null, '\n-- end --\n');  // optional final output
    },
});
```

When the readable side is full, `transform()` is not called again until the output is read (backpressure).

### PassThrough
A Transform that outputs its input unchanged.

### `stream.isReadable(stream)`, `stream.isErrored(stream)`, `stream.getEventListeners(emitter, name)`
Whether a stream can still be read; whether it has failed; the listeners for an event (the same function as in `events`).

### `stream.pipeline(source, ...transforms, destination, callback)`
Pipe the streams together. If any stream fails, all of them are destroyed. `callback(err)` runs once, after every stream has finished or closed. `source` may also be an iterable or async iterable. Returns `destination`.

```javascript
const { pipeline, Transform } = require('stream');

http.createServer((req, res) => {
    pipeline(req, new Transform({
        transform(chunk, encoding, cb) { cb(null, String(chunk).toUpperCase()); },
    }), res, (err) => {
        if (err) console.error('Request failed:', err.message);
    });
}).listen(3000);
```

### `stream.finished(stream[, options], callback)`
Call `callback(err)` once the stream has ended (readable), finished (writable), errored, or closed early (`ERR_STREAM_PREMATURE_CLOSE`). `options.readable`/`options.writable` select which side to wait for. Returns a function that removes the listeners.

### `stream.promises.pipeline(...)` / `stream.promises.finished(stream[, options])`
Promise versions of `pipeline()` and `finished()`.

---

## Buffer API

`Buffer` is a global (also `require('buffer').Buffer`) and a subclass of `Uint8Array`: it can be indexed and iterated, and works anywhere a `Uint8Array` does. Memory belongs to V8 and is freed with the last Buffer or view using it. New memory is always zero-filled, including `allocUnsafe`.

### Creating
| Function | |
|----------|---|
| `Buffer.alloc(size[, fill[, encoding]])` | A zero-filled Buffer, or filled with `fill`. A bad size throws `ERR_OUT_OF_RANGE` |
| `Buffer.allocUnsafe(size)`, `Buffer.allocUnsafeSlow(size)` | Zero-filled too. `allocUnsafe` may come from a shared 8 KB pool (`Buffer.poolSize`), `allocUnsafeSlow` never does |
| `Buffer.from(string[, encoding])` | Encode a string (default UTF-8) |
| `Buffer.from(arrayBuffer[, byteOffset[, length]])` | A **view** of that memory |
| `Buffer.from(array \| typedArray \| buffer \| dataView \| sharedArrayBuffer)` | A copy. Array and typed array elements wrap modulo 256 |
| `Buffer.concat(list[, totalLength])` | Join into a new Buffer, truncated or zero-padded to `totalLength` |
| `new Buffer(...)` | Deprecated, as in Node.js: use `alloc` and `from` |

Small Buffers from `from()`, `concat()` and `allocUnsafe()` share a block, so `buf.buffer` can be larger than the Buffer: use `buf.byteOffset` and `buf.length` with it, and don't transfer it to a worker.

### Class methods
`Buffer.isBuffer(value)`, `Buffer.isEncoding(name)`, `Buffer.byteLength(stringOrBinary[, encoding])`, `Buffer.compare(a, b)` (-1, 0, 1).

### Instance methods
| Method | |
|--------|---|
| `buf.toString([encoding[, start[, end]]])`, `buf.toLocaleString()` | Decode; invalid UTF-8 becomes U+FFFD. A result over V8's string limit throws `ERR_STRING_TOO_LONG` |
| `buf.toJSON()` | `{ type: 'Buffer', data: [...] }`, which `Buffer.from` accepts |
| `buf.write(string[, offset[, length]][, encoding])` | Returns the bytes written. A character that doesn't fit isn't written partly |
| `buf.slice([start[, end]])`, `buf.subarray([start[, end]])` | **Views** of the same memory |
| `buf.equals(other)`, `buf.compare(target[, targetStart[, targetEnd[, sourceStart[, sourceEnd]]]])` | |
| `buf.copy(target[, targetStart[, sourceStart[, sourceEnd]]])` | Returns the bytes copied |
| `buf.fill(value[, offset[, end]][, encoding])` | |
| `buf.indexOf(value[, byteOffset][, encoding])`, `buf.lastIndexOf(...)`, `buf.includes(...)` | `value` is a string, byte or Buffer |

### Inherited from Uint8Array
`length`, `buffer`, `byteOffset`, `byteLength`, `at`, `copyWithin`, `entries`, `every`, `filter`, `find`, `findIndex`, `findLast`, `findLastIndex`, `forEach`, `join`, `keys`, `map`, `reduce`, `reduceRight`, `reverse`, `set`, `some`, `sort`, `toReversed`, `toSorted`, `valueOf`, `values`, `with`.

### Numbers
All take an `offset` (default 0) and throw `ERR_OUT_OF_RANGE` outside the Buffer; writes also check the value and return the offset after it. `UInt` methods are also spelled `Uint`.

`readUInt8`, `readInt8`, `writeUInt8`, `writeInt8`;
`readUInt16LE`, `readUInt16BE`, `readInt16LE`, `readInt16BE`, `writeUInt16LE`, `writeUInt16BE`, `writeInt16LE`, `writeInt16BE`;
`readUInt32LE`, `readUInt32BE`, `readInt32LE`, `readInt32BE`, `writeUInt32LE`, `writeUInt32BE`, `writeInt32LE`, `writeInt32BE`;
`readBigUInt64LE`, `readBigUInt64BE`, `readBigInt64LE`, `readBigInt64BE`, `writeBigUInt64LE`, `writeBigUInt64BE`, `writeBigInt64LE`, `writeBigInt64BE`;
`readFloatLE`, `readFloatBE`, `writeFloatLE`, `writeFloatBE`, `readDoubleLE`, `readDoubleBE`, `writeDoubleLE`, `writeDoubleBE`;
`readUint8`, `writeUint8`, `readUint16LE`, `readUint16BE`, `writeUint16LE`, `writeUint16BE`, `readUint32LE`, `readUint32BE`, `writeUint32LE`, `writeUint32BE`, `readBigUint64LE`, `readBigUint64BE`, `writeBigUint64LE`, `writeBigUint64BE`.

---

## Buffer Encodings

`Buffer.from(string, encoding)`, `buf.toString([encoding[, start[, end]]])` and `Buffer.byteLength(string, encoding)` accept:

| Encoding | Description |
|----------|-------------|
| `utf8` (default), `utf-8` | UTF-8 text |
| `hex` | Two hex digits per byte. Decoding stops at the first invalid pair |
| `base64` | Standard Base64 with padding. Decoding also accepts the URL alphabet and skips whitespace |
| `base64url` | URL-safe Base64 without padding |
| `latin1`, `binary` | One byte per character (code points 0-255) |
| `ascii` | Like `latin1`; `toString` clears the high bit |

Names are case-insensitive; an unknown name throws `TypeError [ERR_UNKNOWN_ENCODING]`.

```javascript
Buffer.from('hello').toString('hex');            // '68656c6c6f'
Buffer.from('68656c6c6f', 'hex').toString();     // 'hello'
Buffer.from('hello').toString('base64');         // 'aGVsbG8='
Buffer.from('hello').toString('utf8', 1, 3);     // 'el'
```

`Buffer.from(buffer | typedArray | arrayBuffer)` copies the bytes.

---

## Crypto API

`require('crypto')` (also the global `crypto`). Supported hashes: `md5`, `sha1`, `sha224`, `sha256`, `sha384`, `sha512`. Algorithm names are case-insensitive and also accept the `SHA-256` and `RSA-SHA256` forms.

### `crypto.createHash(algorithm)`
Returns a `Hash`. Throws `Error: Digest method not supported` for other algorithms.

#### `hash.update(data[, inputEncoding])`
Adds `data` (a string, Buffer, TypedArray or DataView). `inputEncoding` applies to strings (default `utf8`). Returns the hash, so calls chain.

#### `hash.digest([encoding])`
Returns the digest as a Buffer, or as a string in `encoding` (`hex`, `base64`, `base64url`, `latin1`). After `digest()`, `update()`, `digest()` and `copy()` throw `Error [ERR_CRYPTO_HASH_FINALIZED]: Digest already called`.

#### `hash.copy()`
Returns a new `Hash` with the current state, e.g. to get intermediate digests.

```javascript
const crypto = require('crypto');
crypto.createHash('sha256').update('hello').digest('hex');
// '2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824'

const hash = crypto.createHash('md5');
hash.update('part 1, ');
hash.update('part 2');
hash.digest('base64');
```

`Hash` is a Transform stream: write data to it and read the digest Buffer after `end()`, or pipe into it.

```javascript
const hash = crypto.createHash('sha1');
hash.on('data', (digest) => console.log(digest.toString('hex')));
request.pipe(hash);
```

### `crypto.createHmac(algorithm, key)`
Returns an `Hmac` (RFC 2104) keyed with `key` (a string, Buffer, TypedArray, DataView or ArrayBuffer). It has the same `update()` and `digest()` methods as `Hash` and is also a Transform stream. A second `digest()` call returns an empty result, as in Node.js. An unsupported algorithm throws `TypeError [ERR_CRYPTO_INVALID_DIGEST]`.

```javascript
const signature = crypto.createHmac('sha256', process.env.WEBHOOK_SECRET)
    .update(body)
    .digest('hex');
```

### `crypto.pbkdf2(password, salt, iterations, keylen, digest, callback)`
Derives a `keylen`-byte key with PBKDF2 (RFC 8018) and HMAC-`digest`, on the libuv thread pool. `callback(err, derivedKey)` receives a Buffer.

- `password`, `salt`: strings, Buffers, TypedArrays, DataViews or ArrayBuffers
- `iterations`: integer from 1 to 2147483647 (use 100,000 or more for passwords)
- `keylen`: integer from 0 to 2147483647
- `digest`: a supported hash name (required)

```javascript
const salt = crypto.randomBytes(16);
crypto.pbkdf2(password, salt, 210000, 64, 'sha512', (err, key) => {
    if (err) throw err;
    store(salt.toString('hex'), key.toString('hex'));
});
```

### `crypto.pbkdf2Sync(password, salt, iterations, keylen, digest)`
Synchronous version; returns the Buffer.

### `crypto.randomBytes(size[, callback])`
Returns `size` cryptographically strong random bytes from the operating system (via `uv_random`). With a callback, the bytes are generated on the thread pool and passed as `callback(err, buf)`. `size` must be a number from 0 to 2147483647.

```javascript
const token = crypto.randomBytes(32).toString('base64url');
```

### `crypto.randomInt([min, ]max[, callback])`
Returns a random integer `n` with `min <= n < max` (`min` defaults to 0), without modulo bias. Both must be safe integers and `max - min` at most 2^48.

### `crypto.randomUUID()`
Returns a random RFC 4122 version 4 UUID string, e.g. `'1b9d6bcd-bbfd-4b2d-9b5d-ab8dfbbd4bed'`.

### `crypto.getRandomValues(typedArray)`
Fills an integer TypedArray (`Uint8Array`, `Int32Array`, `BigUint64Array`, ...) with random values in place and returns it. At most 65,536 bytes per call.

### `crypto.timingSafeEqual(a, b)`
Compares two Buffers, TypedArrays, DataViews or ArrayBuffers in constant time, so the time taken doesn't reveal where they differ. Use it to check tokens and signatures. Throws `RangeError [ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH]` if the lengths differ.

```javascript
const expected = Buffer.from(computeSignature(body));
const received = Buffer.from(request.headers['x-signature'] || '');
const valid = expected.length === received.length && crypto.timingSafeEqual(expected, received);
```

### `crypto.getHashes()`
Returns the supported hash names.

---

## Module System

CommonJS, following Node.js's algorithm. ES modules (`import`) aren't supported; TypeScript or a bundler can compile them to CommonJS.

### `require(id)`
1. **Built-in**: `fs`, `net`, `http`, `path`, `os`, `events`, `stream`, `util`, `crypto`, `child_process`, `worker_threads`, `buffer`, `timers`, `process`, `module`, optionally with a `node:` prefix. An unknown `node:` name throws `ERR_UNKNOWN_BUILTIN_MODULE`.
2. **A path** (`./`, `../`, `/`), from the requiring module's directory: the exact file, then with `.js` and `.json`, then as a directory: `package.json`'s `main`, then `index.js` and `index.json`.
3. **A package**: the same in `node_modules` of the module's directory and each parent, including scoped names (`@scope/pkg`) and subpaths (`pkg/lib/x`). `package.json`'s `exports` is honored for exact subpaths and the `require`, `node` and `default` conditions (not patterns with `*`). A subpath it doesn't list throws `ERR_PACKAGE_PATH_NOT_EXPORTED`, and a target leaving the package throws `ERR_INVALID_PACKAGE_TARGET`.

Not found throws `MODULE_NOT_FOUND` with `requireStack`. `.json` files are parsed; anything else runs as JavaScript in a function, so top-level `var` stays private and top-level `return` works. A leading BOM and `#!` line are ignored.

### `require.resolve(id)`, `require.cache`, `require.main`
The file `id` would load (or the name of a built-in); loaded modules by file name (delete an entry to load it again); the main module.

### `module`, `exports`
`module.exports` is what `require()` returns and can be any value; `exports` starts as the same object, and is `this` at the top level. A module also has `id`, `path`, `filename`, `loaded`, `children`, `paths`, `parent` and `require(id)`.

### Caching and cycles
A module runs once per file. It is cached before it runs, so a circular `require()` gets its **partial** exports, as they are at that moment. A module that throws isn't kept: requiring it again runs it again.

### Errors in modules
Exceptions propagate to the caller of `require()`; uncaught ones are reported with the module's file and line.

### `require('module')`
`builtinModules` lists the built-in names. `createRequire(filename)` returns a `require` that resolves from that file.

---

## Error Handling

### Exceptions and callbacks
Synchronous failures throw; asynchronous ones go to the callback's first argument or a promise rejection, as `Error` objects with `code`.

```javascript
try {
  fs.readFileSync('missing.txt');
} catch (err) {
  console.error(err.code);    // 'ENOENT'
}

fs.readFile('missing.txt', (err, data) => {
  if (err) return console.error(err.message);
});
```

### Uncaught errors
An exception nobody catches, or a rejected promise without a handler after the microtasks ran, prints a report to stderr (`file:line`, the source line, the stack) and ends the process with exit code 1, wherever it happened: the main script, a timer, an fs callback, a socket event. Exit codes:

| Code | Meaning |
|------|---------|
| 0 | Normal end, or `process.exitCode` otherwise |
| 1 | Uncaught exception or unhandled rejection |
| 7 | An `'uncaughtException'` or `'exit'` listener threw |
| 9 | Invalid command-line option |

`process.on('uncaughtException', ...)` and `process.on('unhandledRejection', ...)` take over. On a worker thread the error ends the worker and becomes the `Worker`'s `'error'` event. In the REPL it is printed and the session goes on.

HTTP handlers are a deliberate exception: a `'request'` listener that throws has the error printed and the client gets a `500`, because one bad request shouldn't end a server. See [HTTP API](#http-api).

---

## Type Conversions

### JavaScript to C++
- **String** → `std::string` (via `String::Utf8Value`)
- **Number** → `int` or `double` (via `Number` methods)
- **Function** → `Persistent<Function>` (for callbacks)
- **Object** → `Local<Object>` (for structured data)

### C++ to JavaScript
- **`std::string`** → String (via `String::NewFromUtf8`)
- **`int`/`double`** → Number (via `Number::New`)
- **Functions** → JavaScript functions (via `FunctionTemplate`)
- **Objects** → JavaScript objects (via `Object::New`)

---

## Examples

### Complete TCP Echo Server
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
    console.log('Echo server listening on port 3000');
});
```

### File Operations

```javascript
fs.mkdirSync('data', { recursive: true });
fs.writeFileSync('data/notes.txt', 'first line\n');
fs.appendFileSync('data/notes.txt', 'second line\n');
console.log(fs.readFileSync('data/notes.txt', 'utf8'));

for (const entry of fs.readdirSync('data', { withFileTypes: true })) {
  console.log(entry.name, entry.isFile() ? 'file' : 'other', fs.statSync('data/' + entry.name).size);
}
fs.rmSync('data', { recursive: true });
```

### Module Usage
```javascript
// logger.js
const log = (message) => {
    console.log(`[LOG] ${message}`);
};
module.exports = { log };

// app.js
const logger = require('./logger');
logger.log('Application started');

// math/utils.js
const add = (a, b) => a + b;
module.exports = { add };

// app.js
const math = require('./math/utils');
console.log(math.add(1, 2)); // 3
```

---

**Last Updated:** 2026-10-16
**API Version:** 1.0.0

---

## Child Process API

`require('child_process')` (also the global `child_process`). Errors, messages and events match Node.js.

### `child_process.spawn(command[, args][, options])`
Start `command` and return a `ChildProcess` straight away.

| Option | Default | Description |
|--------|---------|-------------|
| `cwd` | current directory | Working directory of the child (string or `file:` URL) |
| `env` | `process.env` | Environment variables |
| `argv0` | `command` | `argv[0]` seen by the child |
| `stdio` | `'pipe'` | `'pipe'`, `'inherit'`, `'ignore'`, or an array with one entry per descriptor: one of those, a file descriptor number, or a stream with an `fd` (e.g. `process.stderr`) |
| `shell` | `false` | `true` (`/bin/sh`) or a shell path: run `command` and `args` through the shell |
| `detached` | `false` | Make the child the leader of a new process group |
| `uid`, `gid` | | Run as this user / group |
| `timeout` | | Milliseconds before the child is sent `killSignal` |
| `killSignal` | `'SIGTERM'` | Signal for `timeout` |

```javascript
const { spawn } = require('child_process');
const child = spawn('sh', ['-c', 'echo out; echo err >&2; exit 3']);
child.stdout.on('data', (data) => console.log(`stdout: ${data}`));
child.stderr.pipe(process.stderr);
child.on('close', (code, signal) => console.log('exited', code, signal)); // exited 3 null
```

### ChildProcess
- `child.pid` - process ID (`undefined` if spawning failed)
- `child.stdin` - Writable stream (`null` unless stdio[0] is `'pipe'`); `end()` closes the child's input
- `child.stdout`, `child.stderr` - Readable streams (`setEncoding()`, `pipe()`, `for await`)
- `child.stdio` - `[stdin, stdout, stderr, ...]`
- `child.exitCode`, `child.signalCode`, `child.killed`, `child.spawnfile`, `child.spawnargs`
- `child.kill([signal])` - send a signal (default `'SIGTERM'`; name or number); returns `false` if the child has already exited
- `child.ref()`, `child.unref()` - whether the child keeps the event loop alive

Events:
- `'spawn'` - the child started
- `'exit'` `(code, signal)` - the child ended (one of the two is `null`)
- `'close'` `(code, signal)` - after `'exit'` and the closing of all its stdio streams
- `'error'` `(err)` - the child could not be started (`spawn ls ENOENT`, with `code`, `errno`, `syscall`, `path`, `spawnargs`) or killed

### `child_process.exec(command[, options][, callback])`
Run `command` in `/bin/sh` and buffer its output: `callback(err, stdout, stderr)`. On failure `err` is `Command failed: <command>\n<stderr>` with `code` (the exit code), `killed`, `signal` and `cmd`.

| Option | Default | Description |
|--------|---------|-------------|
| `encoding` | `'utf8'` | Encoding of `stdout`/`stderr`; `'buffer'` for Buffers |
| `timeout` | `0` | Kill the child after this many milliseconds |
| `maxBuffer` | `1048576` | Bytes allowed on stdout or stderr before the child is killed (`ERR_CHILD_PROCESS_STDIO_MAXBUFFER`) |
| `killSignal` | `'SIGTERM'` | Signal used by `timeout` and `maxBuffer` |
| `cwd`, `env`, `shell`, `uid`, `gid` | | As for `spawn()` |

```javascript
const { exec } = require('child_process');
exec('ls | wc -l', (err, stdout) => console.log(stdout.trim()));

const util = require('util');
const { stdout, stderr } = await util.promisify(exec)('git status --short');
```

### `child_process.execFile(file[, args][, options][, callback])`
Like `exec()` but runs `file` directly, without a shell (unless `shell` is set).

### `child_process.spawnSync(command[, args][, options])`
Run the child and wait for it. Returns `{ pid, output, stdout, stderr, status, signal, error }`; `error` is set when the child could not be started (`ENOENT`), timed out (`ETIMEDOUT`) or produced more than `maxBuffer` bytes (`ENOBUFS`). Extra options: `input` (string or Buffer written to stdin), `encoding`, `maxBuffer` (default `1048576`).

### `child_process.execSync(command[, options])`, `child_process.execFileSync(file[, args][, options])`
Like `spawnSync()` but return stdout (a Buffer, or a string with `encoding`) and throw if the child fails: `Command failed: <command>` plus stderr, with `status`, `signal`, `output`, `pid`, `stdout` and `stderr`. Unless `stdio` is given, the child's stderr goes to the parent's stderr.

```javascript
const { execSync } = require('child_process');
const branch = execSync('git branch --show-current', { encoding: 'utf8' }).trim();
const upper = execSync('tr a-z A-Z', { input: 'frog' }).toString(); // 'FROG'
```

Not supported: `fork()` and IPC channels (`stdio: 'ipc'`, `child.send()`), `signal` (AbortSignal) options.

---

## Worker Threads API

`require('worker_threads')` (also the global `worker_threads`). A `Worker` runs JavaScript on another thread, in its own V8 isolate with its own event loop and its own copy of the built-in modules. Threads share no JavaScript objects: they communicate by message, and can share memory through `SharedArrayBuffer`.

### `new Worker(filename[, options])`
`filename` is an absolute path, a path starting with `./` or `../` (resolved against the current directory), or a `file:` URL. With `eval: true` it is the code to run.

| Option | Description |
|--------|-------------|
| `eval` | `filename` is JavaScript code |
| `workerData` | Any cloneable value; the worker's `require('worker_threads').workerData` |
| `transferList` | `ArrayBuffer`s and `MessagePort`s in `workerData` to move instead of copy |
| `argv` | Strings appended to the worker's `process.argv` |

```javascript
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');

if (isMainThread) {
    const worker = new Worker(__filename, { workerData: { start: 2 } });
    worker.on('message', (result) => console.log('result', result)); // result 4
    worker.on('error', (err) => console.error(err));
    worker.on('exit', (code) => console.log('exit', code));           // exit 0
} else {
    parentPort.postMessage(workerData.start * 2);
}
```

Worker methods and properties:
- `worker.postMessage(value[, transferList])` - send a message to the worker's `parentPort`
- `worker.terminate()` - stop the worker, even in the middle of running code; returns a Promise of the exit code (`1`)
- `worker.ref()`, `worker.unref()` - whether the worker keeps this thread's event loop alive (a worker that is unref'd is stopped when the process exits)
- `worker.threadId` - its thread ID (`-1` once it has exited)

Events: `'online'`, `'message'` `(value)`, `'error'` `(err)` (an uncaught exception in the worker, which then exits with code 1), `'exit'` `(exitCode)`. Messages the worker sent before it ended arrive before `'exit'`.

In the worker, `process.exit(code)` ends just the worker (its `'exit'` code is `code`). `console.log` writes straight to the process's stdout.

### `isMainThread`, `threadId`, `parentPort`, `workerData`
- `isMainThread` - `false` inside a worker
- `threadId` - `0` on the main thread, a unique number in each worker
- `parentPort` - inside a worker, the `MessagePort` connected to the `Worker` object (`null` on the main thread). A `'message'` listener on it keeps the worker running until `parentPort.close()` or `worker.terminate()`
- `workerData` - a clone of the `workerData` option (`null` on the main thread)

### `MessageChannel`, `MessagePort`
`new MessageChannel()` creates two connected ports, `port1` and `port2`. A port can be transferred to another thread (put it in a message and in the transfer list) and used there.

- `port.postMessage(value[, transferList])` - send to the other port
- `port.on('message', listener)` / `port.onmessage = (event) => event.data` - receive; adding a listener starts delivery
- `port.start()`, `port.close()` (the other port also closes; both emit `'close'`), `port.ref()`, `port.unref()`, `port.hasRef()`

```javascript
const { Worker, MessageChannel } = require('worker_threads');
const { port1, port2 } = new MessageChannel();
const worker = new Worker(`
    require('worker_threads').parentPort.once('message', ({ port }) => port.postMessage('hello from the worker'));
`, { eval: true });
worker.postMessage({ port: port2 }, [port2]);
port1.once('message', (message) => { console.log(message); port1.close(); });
```

### `receiveMessageOnPort(port)`
Take the next queued message synchronously: `{ message }`, or `undefined` if there is none.

### What can be sent
Messages are copied with the structured clone algorithm: primitives (including `BigInt`), plain objects, arrays, `Map`, `Set`, `Date`, `RegExp`, `Error`s, `ArrayBuffer`s and typed arrays. Buffers arrive as `Uint8Array`s and class instances as plain objects. Functions and native objects can't be sent (`DataCloneError`).

- **`SharedArrayBuffer`** - not copied: both threads see the same memory. Use `Atomics` to synchronize.
- **Transfer list** - `ArrayBuffer`s listed are moved (the sender's copy is detached, with `byteLength` 0); `MessagePort`s must be listed to be sent.

```javascript
const shared = new Int32Array(new SharedArrayBuffer(4));
const worker = new Worker(`
    const { workerData } = require('worker_threads');
    Atomics.add(new Int32Array(workerData), 0, 10);
`, { eval: true, workerData: shared.buffer });
worker.on('exit', () => console.log(Atomics.load(shared, 0))); // 10
```

Not supported: `resourceLimits`, `worker.stdout`/`worker.stderr`/`worker.stdin`, `BroadcastChannel`, `setEnvironmentData`, ES module workers.
