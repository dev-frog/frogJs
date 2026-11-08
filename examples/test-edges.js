// Argument errors and less-traveled paths of events, crypto and worker_threads.
// Exits with code 1 if a check fails. Also runs under Node.js.
const crypto = require('crypto');
const EventEmitter = require('events');
const { spawnSync } = require('child_process');
const { Readable } = require('stream');
const { MessageChannel, Worker, receiveMessageOnPort } = require('worker_threads');
const { check, throws, rejects, section, once, run } = require('./lib/check');

run(async () => {
  section('events');
  check('defaultMaxListeners', typeof EventEmitter.defaultMaxListeners, 'number');
  const original = EventEmitter.defaultMaxListeners;
  EventEmitter.defaultMaxListeners = 5;
  check('defaultMaxListeners set', new EventEmitter().getMaxListeners(), 5);
  EventEmitter.defaultMaxListeners = original;
  throws('bad defaultMaxListeners', () => {
    EventEmitter.defaultMaxListeners = -1;
  }, { code: 'ERR_OUT_OF_RANGE' });
  const emitter = new EventEmitter();
  const listener = () => {};
  emitter.on('x', listener);
  emitter.once('x', listener);
  check('listenerCount of a listener', emitter.listenerCount('x', listener), 2);
  check('getEventListeners', EventEmitter.getEventListeners(emitter, 'x').length, 2);
  emitter.removeAllListeners();
  check('removeAllListeners', emitter.eventNames(), []);
  const warned = spawnSync(process.execPath, ['-e', "const e = new (require('events'))(); e.setMaxListeners(1); e.on('x', () => {}); e.on('x', () => {})"], { encoding: 'utf8' });
  check('too many listeners warns', /MaxListenersExceededWarning/.test(warned.stderr), true);

  const source = new EventEmitter();
  const iterator = EventEmitter.on(source, 'data');
  source.emit('data', 1);
  check('events.on value', (await iterator.next()).value, [1]);
  const failure = new Error('stream failed');
  source.emit('error', failure);
  await rejects('events.on error', iterator.next(), { message: 'stream failed' });
  const closing = EventEmitter.on(new EventEmitter(), 'data');
  closing.throw(new Error('stop'));
  await rejects('events.on after throw()', closing.next(), { message: 'stop' });
  const waiting = new EventEmitter();
  const pendingIterator = EventEmitter.on(waiting, 'data');
  const pendingNext = pendingIterator.next();
  waiting.emit('error', new Error('while waiting'));
  await rejects('events.on error while waiting', pendingNext, { message: 'while waiting' });

  section('crypto');
  const streamed = crypto.createHmac('sha256', 'key');
  const chunks = [];
  streamed.on('data', (chunk) => chunks.push(chunk));
  const done = once(streamed, 'end');
  Readable.from(['a', 'b']).pipe(streamed);
  await done;
  check('Hmac as a stream', Buffer.concat(chunks).toString('hex'), crypto.createHmac('sha256', 'key').update('ab').digest('hex'));
  throws('hash of nothing', () => crypto.createHash(1), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('hmac with an object key', () => crypto.createHmac('sha256', {}), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('hash update with an object', () => crypto.createHash('sha256').update({}), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('hash update with a class instance', () => crypto.createHash('sha256').update(new Map()), { code: 'ERR_INVALID_ARG_TYPE' });
  check('randomInt with a callback', await new Promise((resolve) => crypto.randomInt(5, (err, n) => resolve(n >= 0 && n < 5))), true);
  check('randomInt range', await new Promise((resolve) => crypto.randomInt(2, 3, (err, n) => resolve(n))), 2);

  section('worker_threads');
  throws('worker filename type', () => new Worker(1), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('worker filename object', () => new Worker({}), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('worker filename function', () => new Worker(() => {}), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('worker relative filename', () => new Worker('a-relative-path.js'), { code: 'ERR_WORKER_PATH' });
  const { port1, port2 } = new MessageChannel();
  throws('transfer that is not a list', () => port1.postMessage(1, 1), { name: 'TypeError' });
  throws('options.transfer that is not a list', () => port1.postMessage(1, { transfer: 1 }), { name: 'TypeError' });
  port1.postMessage('x', {});
  check('empty options', receiveMessageOnPort(port2).message, 'x');
  port1.postMessage('y', { transfer: [] });
  check('options.transfer', receiveMessageOnPort(port2).message, 'y');
  throws('receiveMessageOnPort of nothing', () => receiveMessageOnPort({}), { code: 'ERR_INVALID_ARG_TYPE' });
  port2.on('message', () => {});
  port2.removeAllListeners();
  check('a port without listeners', port2.listenerCount('message'), 0);
  port1.close();
  port2.close();
});
