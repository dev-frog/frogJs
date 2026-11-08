// net self-test: exits with code 1 if a check fails. Also runs under Node.js.
const net = require('net');
const stream = require('stream');
const { check, throws, checkError, section, sleep, once, run } = require('./lib/check');

function listen(server, ...args) {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(...args, () => {
      server.removeListener('error', reject);
      resolve(server.address().port);
    });
  });
}

function closeServer(server) {
  return new Promise((resolve) => server.close(() => resolve()));
}

function readAll(socket) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    socket.on('data', (chunk) => chunks.push(chunk));
    socket.on('end', () => resolve(Buffer.concat(chunks)));
    socket.on('error', reject);
  });
}

run(async () => {
  section('Addresses');
  check('isIP', [net.isIP('1.2.3.4'), net.isIP('::1'), net.isIP('nope'), net.isIP('1.2.3.256'), net.isIP('')], [4, 6, 0, 0, 0]);
  check('isIPv4', [net.isIPv4('1.2.3.4'), net.isIPv4('::1'), net.isIPv4(1)], [true, false, false]);
  check('isIPv6', [net.isIPv6('::1'), net.isIPv6('1.2.3.4'), net.isIPv6('zz::1')], [true, false, false]);

  section('Echo');
  const echo = net.createServer((socket) => {
    socket.on('error', () => {});  // clients here may reset the connection
    socket.pipe(socket);
  });
  check('server is an EventEmitter', typeof echo.on, 'function');
  check('no address before listening', echo.address(), null);
  const port = await listen(echo, 0, '127.0.0.1');
  check('address', echo.address().address, '127.0.0.1');
  check('family', echo.address().family, 'IPv4');
  check('listening', echo.listening, true);
  const client = net.connect(port, '127.0.0.1');
  check('socket is a Duplex', client instanceof stream.Duplex, true);
  check('is a net.Socket', client instanceof net.Socket, true);
  check('connecting', client.connecting, true);
  await once(client, 'connect');
  check('connected', client.connecting, false);
  check('remoteAddress', client.remoteAddress, '127.0.0.1');
  check('remotePort', client.remotePort, port);
  check('remoteFamily', client.remoteFamily, 'IPv4');
  check('localAddress', client.localAddress, '127.0.0.1');
  check('localFamily', client.localFamily, 'IPv4');
  check('localPort', typeof client.localPort, 'number');
  check('address()', client.address().address, '127.0.0.1');
  check('readyState', client.readyState, 'open');
  check('pending', client.pending, false);
  const answer = readAll(client);
  client.write('héllo ');
  client.write(Buffer.from([0, 255]));
  client.write('ff', 'hex');
  client.end('end');
  const received = await answer;
  check('echoed bytes', received.equals(Buffer.concat([Buffer.from('héllo '), Buffer.from([0, 255, 255]), Buffer.from('end')])), true);
  await once(client, 'close').catch(() => {});
  check('bytesWritten', client.bytesWritten, received.length);
  check('bytesRead', client.bytesRead, received.length);
  check('destroyed', client.destroyed, true);
  check('readyState closed', client.readyState, 'closed');

  section('Encodings and events');
  const textClient = net.connect({ port, host: '127.0.0.1' });
  textClient.setEncoding('utf8');
  let text = '';
  textClient.on('data', (chunk) => {
    check('setEncoding gives strings', typeof chunk, 'string');
    text += chunk;
    textClient.end();
  });
  textClient.write('€');
  await once(textClient, 'close');
  check('multi-byte text', text, '€');

  const latin = net.connect(port, '127.0.0.1');
  latin.setEncoding('latin1');
  latin.write(Buffer.from([233]));
  check('latin1', (await once(latin, 'data'))[0], 'é');
  latin.destroy();
  const [hadError] = await once(latin, 'close');
  check('close without error', hadError, false);

  const withCallback = await new Promise((resolve) => {
    const s = net.connect(port, '127.0.0.1', () => {
      s.destroy();
      resolve('connected');
    });
  });
  check('connect listener', withCallback, 'connected');
  const created = net.createConnection({ port, host: '127.0.0.1' });
  await once(created, 'connect').catch(() => {});
  check('resolves localhost', created.remotePort, port);
  created.destroy();

  if (process.versions.frogjs) {
    // Node.js prefers ::1 for localhost, which this IPv4 server doesn't listen on
    const named = net.connect(port, 'localhost');
    await once(named, 'connect');
    check('resolves localhost', named.remoteAddress, '127.0.0.1');
    named.destroy();
  }

  section('Half open');
  const half = net.createServer({ allowHalfOpen: true }, (socket) => {
    socket.on('error', () => {});
    const parts = [];
    socket.on('data', (d) => parts.push(d));
    socket.on('end', () => {
      socket.end('got ' + Buffer.concat(parts).toString());
    });
  });
  const halfPort = await listen(half, 0, '127.0.0.1');
  const halfClient = net.connect(halfPort, '127.0.0.1');
  const halfAnswer = readAll(halfClient);
  halfClient.end('ping');
  check('answers after the peer ended', (await halfAnswer).toString(), 'got ping');
  await closeServer(half);

  section('Backpressure');
  let peerSocket;
  const slow = net.createServer((socket) => {
    peerSocket = socket;
    socket.on('error', () => {});
    socket.pause();
  });
  const slowPort = await listen(slow, 0, '127.0.0.1');
  const writer = net.connect(slowPort, '127.0.0.1');
  await once(writer, 'connect');
  await sleep(5);
  const chunk = Buffer.alloc(1024 * 1024, 'x');
  let writes = 0;
  while (writer.write(chunk) && writes < 200) writes++;
  check('write() returns false when the socket is full', writes < 200, true);
  const drained = once(writer, 'drain');
  let total = 0;
  peerSocket.on('data', (d) => {
    total += d.length;
  });
  peerSocket.resume();
  await drained;
  check('drain after the peer reads', total > 0, true);
  writer.destroy();
  await once(peerSocket, 'close').catch(() => {});
  await closeServer(slow);

  section('Reads stop at the highWaterMark');
  let sent = 0;
  const flood = net.createServer((socket) => {
    socket.on('error', () => {});
    const data = Buffer.alloc(65536);
    const pump = () => {
      while (sent < 64 * 1024 * 1024 && socket.write(data)) sent += data.length;
    };
    socket.on('drain', pump);
    pump();
  });
  const floodPort = await listen(flood, 0, '127.0.0.1');
  const reader = net.connect(floodPort, '127.0.0.1');
  reader.pause();
  await once(reader, 'connect');
  await sleep(50);
  check('a paused reader stops the sender', sent < 64 * 1024 * 1024, true);
  reader.destroy();
  await sleep(5);
  flood.close();

  section('Errors');
  const closed = net.createServer();
  const freePort = await listen(closed, 0, '127.0.0.1');
  await closeServer(closed);
  const refused = net.connect(freePort, '127.0.0.1');
  const [refusedError] = await once(refused, 'error').catch((err) => [err]);
  checkError('ECONNREFUSED', refusedError, {
    code: 'ECONNREFUSED', syscall: 'connect', address: '127.0.0.1', port: freePort,
    message: `connect ECONNREFUSED 127.0.0.1:${freePort}`,
  });
  check('errno', typeof refusedError.errno, 'number');
  const [refusedClose] = await once(refused, 'close');
  check('close reports the error', refusedClose, true);

  const badHost = net.connect(80, 'no-such-host.invalid');
  const [dnsError] = await once(badHost, 'error').catch((err) => [err]);
  check('DNS failure', ['ENOTFOUND', 'EAI_NONAME', 'EAI_AGAIN'].includes(dnsError.code) || /getaddrinfo/.test(dnsError.syscall), true);
  check('DNS syscall', dnsError.syscall, 'getaddrinfo');
  check('DNS hostname', dnsError.hostname, 'no-such-host.invalid');

  throws('bad port', () => net.connect(70000), { name: 'RangeError', code: 'ERR_SOCKET_BAD_PORT' });
  throws('negative port', () => net.connect(-1), { code: 'ERR_SOCKET_BAD_PORT' });
  throws('bad listen port', () => net.createServer().listen(70000), { code: 'ERR_SOCKET_BAD_PORT' });
  const stringPort = net.createServer();
  await listen(stringPort, '0', '127.0.0.1');
  await closeServer(stringPort);

  const taken = net.createServer();
  const dup = net.createServer();
  const takenPort = await listen(taken, 0, '127.0.0.1');
  const [inUse] = await once(dup.listen(takenPort, '127.0.0.1'), 'error').catch((err) => [err]);
  checkError('EADDRINUSE', inUse, { code: 'EADDRINUSE', syscall: 'listen', port: takenPort, address: '127.0.0.1' });
  await closeServer(taken);
  const reused = net.createServer();
  await listen(reused, 0, '127.0.0.1');
  throws('listen twice', () => reused.listen(0), { code: 'ERR_SERVER_ALREADY_LISTEN' });
  await closeServer(reused);
  const badBind = net.createServer();
  const [bindError] = await once(badBind.listen(0, '203.0.113.1'), 'error').catch((err) => [err]);
  check('cannot bind an address not ours', bindError.syscall, 'listen');
  const unresolved = net.createServer();
  const [unresolvedError] = await once(unresolved.listen(0, 'no-such-host.invalid'), 'error').catch((err) => [err]);
  check('listen on an unknown host', unresolvedError.syscall, 'getaddrinfo');
  await new Promise((resolve) => net.createServer().close((err) => {
    checkError('close when not running', err, { code: 'ERR_SERVER_NOT_RUNNING' });
    resolve();
  }));

  const writeAfterEnd = net.connect(port, '127.0.0.1');
  await once(writeAfterEnd, 'connect');
  writeAfterEnd.end();
  writeAfterEnd.on('error', () => {});
  writeAfterEnd.write('x', (err) => checkError('write after end', err, { code: 'ERR_STREAM_WRITE_AFTER_END' }));
  await sleep(2);
  writeAfterEnd.destroy();

  section('Writes before the connection and destroy while connecting');
  const queued = net.connect(port, '127.0.0.1');
  queued.write('early');
  check('queued write comes through', (await once(queued, 'data'))[0].toString(), 'early');
  queued.destroy();
  const abort = net.connect(port, '127.0.0.1');
  abort.destroy();
  await once(abort, 'close');
  check('destroyed while connecting', abort.destroyed, true);
  const pendingEnd = net.connect(port, '127.0.0.1');
  pendingEnd.end('bye');
  await once(pendingEnd, 'finish');
  pendingEnd.destroy();
  check('end while connecting', pendingEnd.writableFinished, true);
  const fresh = new net.Socket();
  check('new socket is pending', fresh.pending, true);
  check('no address yet', fresh.address(), {});
  fresh.connect(port, '127.0.0.1');
  if (process.versions.frogjs) {
    throws('connect twice', () => fresh.connect(port, '127.0.0.1'), { code: 'ERR_SOCKET_ALREADY_CONNECTED' });
  }
  check('readyState opening', fresh.readyState, 'opening');
  await once(fresh, 'connect');
  fresh.setNoDelay();
  fresh.setNoDelay(false);
  fresh.setKeepAlive(true, 1000);
  fresh.destroySoon();
  await once(fresh, 'close');
  const unusedSocket = new net.Socket();
  unusedSocket.destroy();
  await once(unusedSocket, 'close');
  check('destroy without a connection', unusedSocket.destroyed, true);

  section('Timeout');
  const idle = net.connect(port, '127.0.0.1');
  await once(idle, 'connect');
  idle.setTimeout(10);
  check('timeout property', idle.timeout, 10);
  await once(idle, 'timeout');
  idle.setTimeout(0);
  const cb = () => {};
  idle.setTimeout(0, cb);
  await new Promise((resolve) => {
    idle.setTimeout(5, resolve);
  });
  idle.destroy();
  const optionTimeout = net.connect({ port, host: '127.0.0.1', timeout: 5 });
  await once(optionTimeout, 'timeout');
  optionTimeout.destroy();

  section('ref and unref');
  const refSocket = net.connect(port, '127.0.0.1');
  check('unref chains', refSocket.unref() === refSocket, true);
  await once(refSocket, 'connect');
  check('ref chains', refSocket.ref() === refSocket, true);
  refSocket.unref().ref();
  refSocket.destroy();
  const refServer = net.createServer();
  check('server unref before listen', refServer.unref() === refServer, true);
  await listen(refServer, 0, '127.0.0.1');
  refServer.ref();
  refServer.unref();
  refServer.ref();
  await closeServer(refServer);

  section('Server options');
  const fn = () => {};
  check('connection listener', net.createServer(fn).listenerCount('connection'), 1);
  check('options and listener', net.createServer({}, fn).listenerCount('connection'), 1);
  check('a listener that is no function is ignored', net.createServer({}, 'x').listenerCount('connection'), 0);
  const objectListen = net.createServer();
  const objectPort = await new Promise((resolve) => objectListen.listen({ port: 0, host: '127.0.0.1', backlog: 5 }, () => resolve(objectListen.address().port)));
  check('listen(options)', typeof objectPort, 'number');
  await closeServer(objectListen);
  const backlogListen = net.createServer();
  await listen(backlogListen, 0, '127.0.0.1', 5);
  await closeServer(backlogListen);
  const noHost = net.createServer();
  await listen(noHost, 0);
  await closeServer(noHost);
  const v6 = net.createServer();
  try {
    await listen(v6, 0, '::1');
    check('IPv6 family', v6.address().family, 'IPv6');
    const v6Client = net.connect(v6.address().port, '::1');
    await once(v6Client, 'connect');
    check('IPv6 peer', v6Client.remoteFamily, 'IPv6');
    v6Client.destroy();
    await closeServer(v6);
  } catch (err) {
    console.log('  (no IPv6 here: ' + err.code + ')');
  }

  section('maxConnections and getConnections');
  const limited = net.createServer((socket) => socket.on('error', () => {}));
  limited.maxConnections = 1;
  const limitedPort = await listen(limited, 0, '127.0.0.1');
  const first = net.connect(limitedPort, '127.0.0.1');
  await once(first, 'connect');
  await sleep(5);
  check('getConnections', await new Promise((resolve) => limited.getConnections((err, count) => resolve(count))), 1);
  const dropped = once(limited, 'drop');
  const second = net.connect(limitedPort, '127.0.0.1');
  second.on('error', () => {});
  await dropped;
  check('over the limit is dropped', true, true);
  second.destroy();

  section('close waits for connections');
  const events = [];
  const closeEvent = new Promise((resolve) => limited.close(() => {
    events.push('closed');
    resolve();
  }));
  await sleep(5);
  events.push('still open');
  first.destroy();
  await closeEvent;
  check('order', events, ['still open', 'closed']);

  await closeServer(echo);
});
