# FrogJS API Reference

Complete API documentation for FrogJS runtime and built-in modules.

## Table of Contents
- [Global Objects](#global-objects)
- [Console API](#console-api)
- [Timer API](#timer-api)
- [File System API](#file-system-api)
- [Network API](#network-api)
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

#### `server.listen(port, callback)`
Start listening on specified port.

**Parameters:**
- `port` (Number): Port number
- `callback` (Function): Optional callback when listening starts

```javascript
server.listen(3000, () => {
    console.log('Server listening on port 3000');
});
```

#### `server.close()`
Stop accepting connections.

```javascript
server.close();
```

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

#### `socket.end()`
Close the socket.

```javascript
socket.end();
```

### `net.connect(port, host, callback)`
Create TCP client connection.

**Parameters:**
- `port` (Number): Port number
- `host` (String): Hostname (default: '127.0.0.1')
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
Handle connection errors.

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

**Last Updated:** 2025-10-28
**API Version:** 0.1.0
