# FrogJS API Reference

Complete API documentation for FrogJS runtime and built-in modules.

## Table of Contents
- [Global Objects](#global-objects)
- [Console API](#console-api)
- [Timer API](#timer-api)
- [File System API](#file-system-api)
- [Network API](#network-api)
- [HTTP API](#http-api)
- [Events API](#events-api)
- [Stream API](#stream-api)
- [Module System](#module-system)

---

## Global Objects

### `__filename`
The absolute path of the current module.

```javascript
console.log(__filename); // /path/to/current/file.js
```

### `__dirname`
The directory name of the current module.

```javascript
console.log(__dirname); // /path/to/current
```

### `require(id)`
Load modules from the file system or built-in modules.

```javascript
const fs = require('fs');
const localModule = require('./myModule');
```

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

---

## Timer API

### `setTimeout(callback, delay)`
Schedule callback execution after `delay` milliseconds.

**Parameters:**
- `callback` (Function): Function to execute
- `delay` (Number): Milliseconds to wait

**Returns:** Number (timer ID)

```javascript
const timerId = setTimeout(() => {
    console.log('Executed after 1000ms');
}, 1000);
```

### `setInterval(callback, delay)`
Schedule repeated callback execution every `delay` milliseconds.

**Parameters:**
- `callback` (Function): Function to execute
- `delay` (Number): Interval in milliseconds

**Returns:** Number (timer ID)

```javascript
const intervalId = setInterval(() => {
    console.log('Executed every 1000ms');
}, 1000);
```

### `clearTimeout(id)`
Cancel a timeout.

**Parameters:**
- `id` (Number): Timer ID from `setTimeout`

```javascript
const timerId = setTimeout(callback, 1000);
clearTimeout(timerId); // Cancel execution
```

### `clearInterval(id)`
Cancel an interval.

**Parameters:**
- `id` (Number): Timer ID from `setInterval`

```javascript
const intervalId = setInterval(callback, 1000);
clearInterval(intervalId); // Stop interval
```

---

## File System API

### `fs.readFileSync(path)`
Read file contents synchronously.

**Parameters:**
- `path` (String): File path

**Returns:** String (file contents)

**Throws:** Error if file cannot be read

```javascript
const fs = require('fs');
try {
    const data = fs.readFileSync('config.json');
    console.log(data);
} catch (err) {
    console.error('Failed to read file:', err);
}
```

### `fs.writeFileSync(path, data)`
Write data to file synchronously.

**Parameters:**
- `path` (String): File path
- `data` (String): Data to write

**Throws:** Error if file cannot be written

```javascript
const fs = require('fs');
try {
    fs.writeFileSync('output.txt', 'Hello, FrogJS!');
    console.log('File written successfully');
} catch (err) {
    console.error('Failed to write file:', err);
}
```

### `fs.readFile(path, callback)`
Read file contents asynchronously.

**Parameters:**
- `path` (String): File path
- `callback` (Function): `(err, data) => {}`
  - `err` (Error | null): Error if occurred
  - `data` (String): File contents

```javascript
const fs = require('fs');
fs.readFile('config.json', (err, data) => {
    if (err) {
        console.error('Failed to read file:', err);
        return;
    }
    console.log('File contents:', data);
});
```

### `fs.writeFile(path, data, callback)`
Write data to file asynchronously.

**Parameters:**
- `path` (String): File path
- `data` (String): Data to write
- `callback` (Function): `(err) => {}`
  - `err` (Error | null): Error if occurred

```javascript
const fs = require('fs');
fs.writeFile('output.txt', 'Hello, FrogJS!', (err) => {
    if (err) {
        console.error('Failed to write file:', err);
        return;
    }
    console.log('File written successfully');
});
```

---

## Network API

### `net.createServer(callback)`
Create a TCP server.

**Parameters:**
- `callback` (Function): Connection handler `(socket) => {}`
  - `socket` (Object): Socket object for the connection

**Returns:** Server object

```javascript
const net = require('net');
const server = net.createServer((socket) => {
    console.log('New connection');
});
```

### Server Methods

#### `server.listen(port[, host][, callback])`
Start listening on specified port.

**Parameters:**
- `port` (Number): Port number (`0` picks a free port)
- `host` (String): Optional address to bind (default `'0.0.0.0'`)
- `callback` (Function): Optional callback when listening starts

Throws an `Error` with `code` (e.g. `'EADDRINUSE'`) if the port can't be bound.

```javascript
server.listen(3000, () => {
    console.log('Server listening on port 3000');
});
```

#### `server.close([callback])`
Stop accepting connections. `callback` runs once the listening socket is closed.

```javascript
server.close();
```

#### `server.address()`
Returns `{ address, family, port }` for the bound socket.

#### `net.createServer({ allowHalfOpen: true }, callback)`
Keep a connection writable after the peer has ended its side. Call `socket.end()` when done.

### Socket Events

#### `socket.on('data', callback)`
Handle incoming data.

**Parameters:**
- `callback` (Function): `(data) => {}`
  - `data` (String): Received data

```javascript
socket.on('data', (data) => {
    console.log('Received:', data.toString());
});
```

#### `socket.on('end', callback)`
Handle connection end.

**Parameters:**
- `callback` (Function): `() => {}`

```javascript
socket.on('end', () => {
    console.log('Connection ended');
});
```

### Socket Methods

#### `socket.write(data)`
Write data to the socket.

**Parameters:**
- `data` (String): Data to send

```javascript
socket.write('Hello, Client!');
```

#### `socket.write(data, 'latin1')`
Write a byte string (each character is one byte). `data` may also be a `Buffer`.

#### `socket.end([data])`
Write `data` (optional), flush pending writes, then close the socket.

```javascript
socket.end();
```

#### `socket.destroy()`
Close the socket immediately, discarding pending writes.

#### `socket.setEncoding(encoding)`
`'utf8'` (default) or `'latin1'`/`'binary'` to receive data as byte strings.

#### `socket.on('close', callback)`
Emitted once the socket is fully closed, for both client and server sockets.

#### `socket.remoteAddress` / `socket.remotePort`
Address of the peer.

### `net.connect(port, host, callback)`
Create TCP client connection.

**Parameters:**
- `port` (Number): Port number
- `host` (String): Hostname or IP address (default: '127.0.0.1'). Hostnames are resolved via DNS; IPv4 addresses are tried first
- `callback` (Function): Optional connection callback

**Returns:** Socket object

```javascript
const net = require('net');
const client = net.connect(3000, 'localhost', () => {
    console.log('Connected to server');
    client.write('Hello, Server!');
});
```

### Client Socket Events

#### `socket.on('data', callback)`
Handle incoming data from server.

```javascript
client.on('data', (data) => {
    console.log('Server says:', data.toString());
});
```

#### `socket.on('connect', callback)`
Handle successful connection.

```javascript
client.on('connect', () => {
    console.log('Connected!');
});
```

#### `socket.on('error', callback)`
Handle connection errors. `err.code` is e.g. `'ECONNREFUSED'` or `'EAI_NONAME'`.

```javascript
client.on('error', (err) => {
    console.error('Connection error:', err);
});
```

#### `socket.on('end', callback)`
Handle connection end.

```javascript
client.on('end', () => {
    console.log('Disconnected from server');
});
```

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

## Module System

### `require(id)`
Load modules.

**Parameters:**
- `id` (String): Module identifier

**Returns:** Module exports

**Module Resolution:**
1. Built-in modules (not yet implemented)
2. Relative paths (`./`, `../`)
3. Absolute paths
4. Auto-append `.js` extension
5. Auto-append `/index.js` for directories

```javascript
// Relative path
const utils = require('./utils');

// Directory with index.js
const module = require('./my-module');

// Nested path
const nested = require('../parent/module');
```

### `module.exports`
Export values from module.

```javascript
// Export object
module.exports = {
    foo: 'bar',
    func: () => {}
};

// Export single value
module.exports = 'my value';

// Export function
module.exports = () => {
    console.log('Hello!');
};
```

### Module Caching
Modules are cached after first load. Subsequent `require()` calls return the cached module.

```javascript
// First call loads and executes module
const mod1 = require('./module');

// Second call returns cached module
const mod2 = require('./module');

console.log(mod1 === mod2); // true
```

---

## Error Handling

### Try-Catch in JavaScript
```javascript
try {
    const data = fs.readFileSync('file.txt');
} catch (err) {
    console.error('Error:', err.message);
}
```

### Error Callbacks
```javascript
fs.readFile('file.txt', (err, data) => {
    if (err) {
        console.error('Error:', err.message);
        return;
    }
    console.log('Success:', data);
});
```

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
const fs = require('fs');

// Async read and write
fs.readFile('input.txt', (err, data) => {
    if (err) {
        console.error('Read error:', err);
        return;
    }

    fs.writeFile('output.txt', data.toUpperCase(), (err) => {
        if (err) {
            console.error('Write error:', err);
            return;
        }
        console.log('File processed successfully');
    });
});
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
**API Version:** 0.5.0
