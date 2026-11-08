// HTTP robustness self-test: hostile input over raw sockets. Exits with code 1
// if a check fails. FrogJS-specific: Node.js's parser is more lenient in places
// (a request line without a version, for example), and these checks are about
// what FrogJS guarantees.
const http = require('http');
const net = require('net');
const { check, throws, section, sleep, once, run } = require('./lib/check');

const isFrog = !!process.versions.frogjs;

function listen(server) {
  return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(server.address().port)));
}

// Send raw bytes, return what comes back until the connection closes (or a time limit)
function raw(port, data, limit = 2000) {
  return new Promise((resolve) => {
    const socket = net.connect(port, '127.0.0.1');
    let text = '';
    const timer = setTimeout(() => socket.destroy(), limit);
    socket.setEncoding('latin1');
    socket.on('connect', () => socket.write(data, 'latin1'));
    socket.on('data', (chunk) => {
      text += chunk;
    });
    socket.on('error', () => {});
    socket.on('close', () => {
      clearTimeout(timer);
      resolve(text);
    });
  });
}

const statusOf = (text) => Number((/^HTTP\/1\.1 (\d+)/.exec(text) || [])[1]);

run(async () => {
  let seen = [];
  const server = http.createServer((req, res) => {
    const parts = [];
    seen.push(req.url);
    req.on('data', (d) => parts.push(d));
    req.on('end', () => res.end('ok:' + parts.join('')));
  });
  const port = await listen(server);

  section('Well-formed requests still work');
  check('GET', statusOf(await raw(port, 'GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n')), 200);
  check('chunked', (await raw(port, 'POST / HTTP/1.1\r\nHost: a\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n')).endsWith('ok:abc'), true);
  check('chunk extension', (await raw(port, 'POST / HTTP/1.1\r\nHost: a\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n3;x=y\r\nabc\r\n0\r\n\r\n')).endsWith('ok:abc'), true);
  check('leading empty lines', statusOf(await raw(port, '\r\n\r\nGET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n')), 200);
  check('body by Content-Length', (await raw(port, 'POST / HTTP/1.1\r\nHost: a\r\nConnection: close\r\nContent-Length: 2\r\n\r\nhi')).endsWith('ok:hi'), true);

  section('Malformed requests get 400');
  const bad = {
    'garbage': 'NOT HTTP\r\n\r\n',
    'no version': 'GET /\r\n\r\n',
    'space in URL': 'GET /a b HTTP/1.1\r\nHost: a\r\n\r\n',
    'NUL in URL': 'GET /a\0b HTTP/1.1\r\nHost: a\r\n\r\n',
    'control char in URL': 'GET /a\x01b HTTP/1.1\r\nHost: a\r\n\r\n',
    'space before the colon': 'GET / HTTP/1.1\r\nHost : a\r\n\r\n',
    'no colon': 'GET / HTTP/1.1\r\nHost\r\n\r\n',
    'empty header name': 'GET / HTTP/1.1\r\n: a\r\n\r\n',
    'bad header name': 'GET / HTTP/1.1\r\nHo(st: a\r\n\r\n',
    'NUL in a value': 'GET / HTTP/1.1\r\nHost: a\0b\r\n\r\n',
    'bare CR in a value': 'GET / HTTP/1.1\r\nHost: a\rb\r\n\r\n',
    'bare LF in a value': 'GET / HTTP/1.1\r\nX: a\nInjected: b\r\nHost: a\r\n\r\n',
    'obsolete line folding': 'GET / HTTP/1.1\r\nHost: a\r\n  folded\r\n\r\n',
  };
  for (const name of Object.keys(bad)) {
    seen = [];
    const text = await raw(port, bad[name]);
    check(name, statusOf(text), 400);
    check(`${name}: no handler ran`, seen, []);
  }

  section('Request smuggling');
  const smuggling = {
    'Content-Length and Transfer-Encoding': 'POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n',
    'two different Content-Lengths': 'POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab',
    'comma-separated different lengths': 'POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1, 2\r\n\r\nab',
    'negative Content-Length': 'POST / HTTP/1.1\r\nHost: a\r\nContent-Length: -1\r\n\r\n',
    'plus sign in Content-Length': 'POST / HTTP/1.1\r\nHost: a\r\nContent-Length: +1\r\n\r\na',
    'hex Content-Length': 'POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 0x1\r\n\r\na',
    'huge Content-Length': 'POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999999999999999\r\n\r\n',
    'Transfer-Encoding not ending in chunked': 'POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked, gzip\r\n\r\n',
    'unknown Transfer-Encoding': 'POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: xchunked\r\n\r\n',
    'Transfer-Encoding in HTTP/1.0': 'POST / HTTP/1.0\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n',
    'bad chunk size': 'POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n',
    'negative chunk size': 'POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n-1\r\n',
    'missing CRLF after a chunk': 'POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nabc',
    'bad trailer': 'POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nbad trailer\r\n\r\n',
  };
  for (const name of Object.keys(smuggling)) {
    const text = await raw(port, smuggling[name], 300);
    check(name, statusOf(text) === 400 || text === '' || !text.includes('ok:'), true);
  }

  section('Size limits');
  check('header overflow', statusOf(await raw(port, 'GET / HTTP/1.1\r\nX: ' + 'a'.repeat(100000) + '\r\n\r\n')), 431);
  check('unterminated header overflow', statusOf(await raw(port, 'GET /' + 'a'.repeat(100000))), 431);
  check('many small headers', statusOf(await raw(port, 'GET / HTTP/1.1\r\n' + 'A: b\r\n'.repeat(20000) + '\r\n')), 431);
  const oneKb = 'GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\nX: ' + 'a'.repeat(1000) + '\r\n\r\n';
  check('a large but legal header', statusOf(await raw(port, oneKb)), 200);
  if (isFrog) {
    check('endless chunk size line', statusOf(await raw(port, 'POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n' + '1'.repeat(5000), 500)), 400);
    check('bad HTTP version', statusOf(await raw(port, 'GET / HTTP/2.0\r\nHost: a\r\n\r\n')), 505);
  }

  section('Header names can not reach Object.prototype');
  let protoHeaders;
  const protoServer = http.createServer((req, res) => {
    protoHeaders = req.headers;
    res.end();
  });
  const protoPort = await listen(protoServer);
  await raw(protoPort, 'GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n__proto__: polluted\r\nconstructor: x\r\n\r\n');
  check('no pollution', {}.polluted, undefined);
  check('__proto__ is a plain header', protoHeaders.__proto__, 'polluted');
  protoServer.close();

  section('Response splitting');
  const splitServer = http.createServer((req, res) => {
    const errors = [];
    const attempt = (fn) => {
      try {
        fn();
      } catch (err) {
        errors.push(err.code);
      }
    };
    attempt(() => res.setHeader('X-A', 'a\r\nInjected: yes'));
    attempt(() => res.setHeader('X-A\r\nInjected', 'yes'));
    attempt(() => res.setHeader('X-A', ['ok', 'a\nb']));
    attempt(() => res.setHeader('X-A'));
    attempt(() => res.writeHead(200, 'OK\r\nInjected: yes'));
    res.statusMessage = undefined;
    res.end(JSON.stringify(errors));
  });
  const splitPort = await listen(splitServer);
  const split = await raw(splitPort, 'GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n');
  check('nothing injected', split.toLowerCase().includes('injected'), false);
  check('every attempt was rejected', JSON.parse(split.slice(split.indexOf('\r\n\r\n') + 4)), [
    'ERR_INVALID_CHAR', 'ERR_INVALID_HTTP_TOKEN', 'ERR_INVALID_CHAR', 'ERR_HTTP_INVALID_HEADER_VALUE', 'ERR_INVALID_CHAR',
  ]);
  splitServer.close();

  throws('client: bad path', () => http.request({ port, path: '/a b' }), { code: 'ERR_UNESCAPED_CHARACTERS' });
  throws('client: CRLF in a path', () => http.request({ port, path: '/\r\nInjected: x' }), { code: 'ERR_UNESCAPED_CHARACTERS' });
  throws('client: bad method', () => http.request({ port, method: 'GE T' }), { code: 'ERR_INVALID_HTTP_TOKEN' });
  throws('client: bad header', () => http.request({ port, headers: { 'X-A': 'a\r\nb' } }), { code: 'ERR_INVALID_CHAR' });

  section('Memory: a body nobody reads and pipelining');
  let unreadRequests = 0;
  const lazy = http.createServer(() => {
    unreadRequests++;  // never reads the body, never answers
  });
  const lazyPort = await listen(lazy);
  const flood = net.connect(lazyPort, '127.0.0.1');
  flood.on('error', () => {});
  await once(flood, 'connect');
  flood.write('POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1000000000\r\n\r\n');
  const chunk = Buffer.alloc(65536);
  let sent = 0;
  for (let i = 0; i < 400 && sent < 1e9; i++) {
    if (flood.write(chunk)) {
      sent += chunk.length;
      if (i % 16 === 0) await sleep(0);
    } else {
      const done = await Promise.race([once(flood, 'drain').then(() => true), sleep(100).then(() => false)]);
      if (!done) break;
      sent += chunk.length;
    }
  }
  check('the handler ran', unreadRequests, 1);
  check('the sender is held back', sent < 26 * 1024 * 1024, true);
  flood.destroy();
  lazy.close();
  lazy.closeAllConnections();

  let held = 0;
  const pipeliner = http.createServer((req, res) => {
    held++;
    pipelined.push(res);
  });
  const pipelined = [];
  const pipelinePort = await listen(pipeliner);
  const flood2 = net.connect(pipelinePort, '127.0.0.1');
  flood2.on('error', () => {});
  await once(flood2, 'connect');
  const requests = 'GET / HTTP/1.1\r\nHost: a\r\n\r\n'.repeat(3000);
  let pipelineSent = 0;
  for (let i = 0; i < 2000; i++) {
    if (flood2.write(requests)) {
      pipelineSent += requests.length;
      if (i % 16 === 0) await sleep(0);
    } else {
      const done = await Promise.race([once(flood2, 'drain').then(() => true), sleep(100).then(() => false)]);
      if (!done) break;
      pipelineSent += requests.length;
    }
  }
  check('one request at a time', held, 1);
  check('pipelined input is held back', pipelineSent < 40 * 1024 * 1024, true);
  flood2.destroy();
  for (const res of pipelined) res.end();
  pipeliner.close();
  pipeliner.closeAllConnections();

  section('Timeouts');
  const timed = http.createServer((req, res) => {
    req.on('data', () => {});
    req.on('end', () => res.end());
    req.on('error', () => {});
  });
  timed.headersTimeout = 30;
  timed.requestTimeout = 30;
  const timedPort = await listen(timed);
  check('slow headers', statusOf(await raw(timedPort, 'GET / HTTP/1.1\r\nHost:')), 408);
  if (isFrog) {
    check('slow body', statusOf(await raw(timedPort, 'POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 10\r\n\r\nabc')), 408);
  }
  timed.close();

  server.close();
  server.closeAllConnections();
});
