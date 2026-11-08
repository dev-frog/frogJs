// Random and mutated input for the code that parses what it doesn't control: the
// HTTP parser, Buffer and encodings. Run on the sanitizer build with `make fuzz`,
// where a memory error ends the process. FUZZ_SECONDS sets the time per target
// (default 10); FUZZ_SEED repeats a run.
const http = require('http');
const net = require('net');

const SECONDS = Number(process.env.FUZZ_SECONDS) || 10;
const SEED = Number(process.env.FUZZ_SEED) || Date.now() % 2147483647;
console.log(`seed ${SEED}, ${SECONDS} s per target`);

// A small deterministic generator (Park-Miller), so a seed repeats a run
let state = SEED;
function random(n) {
  state = (state * 48271) % 2147483647;
  return state % n;
}
const pick = (list) => list[random(list.length)];

const REQUESTS = [
  'GET / HTTP/1.1\r\nHost: a\r\n\r\n',
  'POST /x HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\n\r\nhello',
  'POST /x HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n5;e=1\r\nhello\r\n0\r\nT: v\r\n\r\n',
  'PUT / HTTP/1.0\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n',
  'GET / HTTP/1.1\r\nHost: a\r\nExpect: 100-continue\r\nCookie: a=b; c=d\r\n\r\n',
];
const FRAGMENTS = ['\r\n', '\n', '\r', '\0', ':', ' ', '\t', ',', ';', '0', '-1', 'chunked', 'Content-Length', 'Transfer-Encoding',
  'HTTP/1.1', 'HTTP/9.9', 'ffffffffffffffff', '99999999999999999999', '\xff\xfe', '%00', '../', '__proto__', 'a'.repeat(5000)];

function mutate(text) {
  let out = text;
  const rounds = 1 + random(8);
  for (let i = 0; i < rounds; i++) {
    const at = random(out.length + 1);
    switch (random(5)) {
      case 0: out = out.slice(0, at) + pick(FRAGMENTS) + out.slice(at); break;
      case 1: out = out.slice(0, at) + out.slice(at + 1 + random(20)); break;
      case 2: out = out.slice(0, at) + String.fromCharCode(random(256)) + out.slice(at + 1); break;
      case 3: out = out.slice(0, at) + out.slice(random(out.length + 1)); break;
      default: out += pick(REQUESTS); break;
    }
  }
  return out;
}

function randomBytes(max) {
  const bytes = Buffer.alloc(random(max));
  for (let i = 0; i < bytes.length; i++) bytes[i] = random(256);
  return bytes;
}

function fuzzBuffer() {
  const encodings = ['utf8', 'hex', 'base64', 'base64url', 'latin1', 'ascii'];
  const numbers = [0, 1, -1, 2 ** 31, -(2 ** 31), 2 ** 53, NaN, Infinity, -Infinity, 0.5, 3, 7, 100, undefined];
  const end = Date.now() + SECONDS * 1000;
  let count = 0;
  const swallow = (fn) => {
    try {
      return fn();
    } catch (err) {
      if (!(err instanceof Error)) throw err;
      return undefined;
    }
  };
  while (Date.now() < end) {
    const bytes = randomBytes(300);
    const encoding = pick(encodings);
    const text = swallow(() => bytes.toString(encoding, pick(numbers), pick(numbers)));
    if (typeof text === 'string') swallow(() => Buffer.from(text, pick(encodings)));
    swallow(() => Buffer.byteLength(bytes.toString('latin1'), encoding));
    swallow(() => bytes.write(bytes.toString('latin1'), pick(numbers), pick(numbers), encoding));
    swallow(() => bytes.slice(pick(numbers), pick(numbers)).toString());
    swallow(() => bytes.fill(pick([0, 'ab', bytes, Buffer.alloc(1)]), pick(numbers), pick(numbers)));
    swallow(() => bytes.indexOf(pick([0, 'a', bytes.slice(0, 3)]), pick(numbers)));
    swallow(() => bytes.lastIndexOf(pick([0, 'a', bytes.slice(0, 3)]), pick(numbers)));
    swallow(() => bytes.copy(Buffer.alloc(random(50)), pick(numbers), pick(numbers), pick(numbers)));
    swallow(() => bytes.compare(Buffer.alloc(random(50)), pick(numbers), pick(numbers), pick(numbers), pick(numbers)));
    swallow(() => bytes.readUInt32LE(pick(numbers)));
    swallow(() => bytes.writeDoubleBE(1.5, pick(numbers)));
    swallow(() => Buffer.concat([bytes, bytes], pick(numbers)));
    swallow(() => Buffer.alloc(pick([0, 1, 16, -1, NaN, 2 ** 53]), pick([0, 'x', bytes])));
    swallow(() => Buffer.from(bytes.buffer, pick(numbers), pick(numbers)));
    count++;
  }
  console.log(`Buffer: ${count} rounds`);
}

function fuzzHttp() {
  return new Promise((resolve) => {
    let handled = 0;
    let connections = 0;
    const server = http.createServer((req, res) => {
      handled++;
      req.on('data', () => {});
      req.on('error', () => {});
      req.on('end', () => res.end('ok'));
      res.on('error', () => {});
    });
    server.headersTimeout = 200;
    server.requestTimeout = 200;
    server.keepAliveTimeout = 50;
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      const end = Date.now() + SECONDS * 1000;
      let done = false;
      const next = () => {
        if (done) return;
        if (Date.now() >= end) {
          done = true;
          console.log(`HTTP: ${connections} connections, ${handled} requests handled`);
          server.close();
          server.closeAllConnections();
          resolve();
          return;
        }
        connections++;
        const socket = net.connect(port, '127.0.0.1');
        const finish = () => {
          clearTimeout(timer);
          socket.removeAllListeners('close');
          next();
        };
        const timer = setTimeout(() => socket.destroy(), 30);
        socket.on('error', () => {});
        socket.on('data', () => {});
        socket.on('close', finish);
        socket.on('connect', () => {
          let text = mutate(pick(REQUESTS));
          // Split at random points: the parser is incremental
          while (text.length > 0) {
            const size = 1 + random(text.length);
            socket.write(text.slice(0, size), 'latin1');
            text = text.slice(size);
          }
          if (random(3) === 0) socket.end();
        });
      };
      for (let i = 0; i < 8; i++) next();
    });
  });
}

fuzzBuffer();
fuzzHttp().then(() => console.log('no crash'));
