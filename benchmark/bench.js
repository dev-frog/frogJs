// One benchmark per process: prints its time in milliseconds. Runs under FrogJS
// and Node.js, so the numbers compare the same operations. `--list` names them.
const crypto = require('crypto');
const EventEmitter = require('events');
const fs = require('fs');
const http = require('http');
const net = require('net');
const os = require('os');
const path = require('path');
const stream = require('stream');
const util = require('util');

const tmp = path.join(os.tmpdir(), `frogjs-bench-${process.pid}`);

const benchmarks = {
  // Pure JavaScript: the same V8 work in both, the control
  'js-loop'() {
    let sum = 0;
    for (let i = 0; i < 50000000; i++) sum = (sum + i * 2) % 1000003;
    return sum;
  },
  'buffer-alloc'() {
    for (let i = 0; i < 300000; i++) Buffer.alloc(64);
  },
  'buffer-from-string'() {
    for (let i = 0; i < 300000; i++) Buffer.from('hello world, this is a frog');
  },
  'buffer-tostring'() {
    const b = Buffer.from('hello world, this is a frog'.repeat(4));
    for (let i = 0; i < 500000; i++) b.toString();
  },
  'buffer-hex-base64'() {
    const b = Buffer.alloc(1024, 'x');
    for (let i = 0; i < 20000; i++) {
      Buffer.from(b.toString('hex'), 'hex');
      Buffer.from(b.toString('base64'), 'base64');
    }
  },
  'buffer-concat'() {
    const part = Buffer.alloc(1024);
    const parts = [part, part, part, part];
    for (let i = 0; i < 100000; i++) Buffer.concat(parts);
  },
  'buffer-read-write'() {
    const b = Buffer.alloc(1024);
    for (let i = 0; i < 2000000; i++) b.writeUInt32LE(b.readUInt32LE(i % 1000) + 1 >>> 0, i % 1000);
  },
  'events-emit'() {
    const e = new EventEmitter();
    let n = 0;
    e.on('x', () => n++);
    e.on('x', () => n++);
    for (let i = 0; i < 5000000; i++) e.emit('x', i);
    return n;
  },
  'util-inspect'() {
    const value = { a: 1, b: 'two', c: [1, 2, 3], d: { e: new Map([[1, 2]]), f: new Date(0) } };
    for (let i = 0; i < 50000; i++) util.inspect(value);
  },
  'path-ops'() {
    for (let i = 0; i < 500000; i++) {
      path.join('/a/b', '../c', 'file.txt');
      path.basename('/a/b/file.txt', '.txt');
      path.resolve('/a', 'b');
    }
  },
  'sha256-small'() {
    for (let i = 0; i < 100000; i++) crypto.createHash('sha256').update('hello').digest('hex');
  },
  'sha256-large'() {
    const data = Buffer.alloc(16 * 1024 * 1024, 'x');
    for (let i = 0; i < 4; i++) crypto.createHash('sha256').update(data).digest();
  },
  'pbkdf2'() {
    crypto.pbkdf2Sync('password', 'salt', 200000, 32, 'sha256');
  },
  'fs-sync'() {
    fs.mkdirSync(tmp, { recursive: true });
    const file = path.join(tmp, 'f');
    for (let i = 0; i < 20000; i++) {
      fs.writeFileSync(file, 'hello');
      fs.readFileSync(file);
      fs.statSync(file);
    }
    fs.rmSync(tmp, { recursive: true });
  },
  async 'fs-async'() {
    fs.mkdirSync(tmp, { recursive: true });
    const file = path.join(tmp, 'f');
    for (let i = 0; i < 10000; i++) {
      await fs.promises.writeFile(file, 'hello');
      await fs.promises.readFile(file);
    }
    fs.rmSync(tmp, { recursive: true });
  },
  async 'fs-stream'() {
    fs.mkdirSync(tmp, { recursive: true });
    const file = path.join(tmp, 'f');
    fs.writeFileSync(file, Buffer.alloc(64 * 1024 * 1024));
    for (let i = 0; i < 4; i++) {
      await new Promise((resolve, reject) => {
        const s = fs.createReadStream(file);
        s.on('data', () => {});
        s.on('close', resolve);
        s.on('error', reject);
      });
    }
    fs.rmSync(tmp, { recursive: true });
  },
  async 'stream-pipe'() {
    const chunk = Buffer.alloc(1024);
    let left = 200000;
    const source = new stream.Readable({
      read() {
        this.push(left-- > 0 ? chunk : null);
      },
    });
    const through = new stream.Transform({
      transform(data, encoding, callback) {
        callback(null, data);
      },
    });
    const sink = new stream.Writable({
      write(data, encoding, callback) {
        callback();
      },
    });
    await new Promise((resolve, reject) => stream.pipeline(source, through, sink, (err) => (err ? reject(err) : resolve())));
  },
  async 'timers'() {
    await new Promise((resolve) => {
      let left = 200000;
      for (let i = 0; i < 200000; i++) {
        setTimeout(() => {
          if (--left === 0) resolve();
        }, 1);
      }
    });
  },
  async 'tcp-throughput'() {
    const server = net.createServer((socket) => socket.resume());
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const socket = net.connect(server.address().port, '127.0.0.1');
    await new Promise((resolve) => socket.once('connect', resolve));
    const chunk = Buffer.alloc(64 * 1024);
    let left = 16000;  // 1 GB
    await new Promise((resolve) => {
      const pump = () => {
        while (left > 0) {
          left--;
          if (!socket.write(chunk)) return;
        }
        socket.end();
        resolve();
      };
      socket.on('drain', pump);
      pump();
    });
    await new Promise((resolve) => socket.once('close', resolve));
    server.close();
  },
  async 'tcp-connections'() {
    const server = net.createServer((socket) => socket.end('x'));
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const { port } = server.address();
    for (let i = 0; i < 300; i++) {
      await Promise.all(Array.from({ length: 10 }, () => new Promise((resolve) => {
        const socket = net.connect(port, '127.0.0.1');
        socket.resume();
        socket.on('close', resolve);
      })));
    }
    server.close();
  },
  async 'http-requests'() {
    const server = http.createServer((req, res) => {
      res.setHeader('Content-Type', 'text/plain');
      res.end('hello world');
    });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const { port } = server.address();
    const one = () => new Promise((resolve, reject) => {
      const req = http.request({ port, host: '127.0.0.1', path: '/' }, (res) => {
        res.resume();
        res.on('end', resolve);
      });
      req.on('error', reject);
      req.end();
    });
    for (let i = 0; i < 300; i++) await Promise.all(Array.from({ length: 10 }, one));
    server.close();
  },
  async 'http-body'() {
    const body = 'x'.repeat(1024 * 1024);
    const server = http.createServer((req, res) => {
      let size = 0;
      req.on('data', (d) => {
        size += d.length;
      });
      req.on('end', () => res.end(String(size)));
    });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const { port } = server.address();
    for (let i = 0; i < 100; i++) {
      await new Promise((resolve, reject) => {
        const req = http.request({ port, host: '127.0.0.1', method: 'POST', path: '/', headers: { 'Content-Length': body.length } }, (res) => {
          res.resume();
          res.on('end', resolve);
        });
        req.on('error', reject);
        req.end(body);
      });
    }
    server.close();
  },
};

const name = process.argv[2];
if (name === '--list') {
  console.log(Object.keys(benchmarks).join('\n'));
} else {
  const start = process.hrtime();
  Promise.resolve().then(() => benchmarks[name]()).then(() => {
    const [seconds, nanos] = process.hrtime(start);
    console.log(seconds * 1000 + nanos / 1e6);
    process.exit(0);
  });
}
