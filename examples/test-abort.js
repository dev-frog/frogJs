// EventTarget, Event, AbortController, DOMException, timers/promises and the
// functions that take an AbortSignal: exits with code 1 if a check fails.
// Also runs under Node.js, which is how the expected values were checked.
const events = require('events');
const fs = require('fs');
const os = require('os');
const path = require('path');
const util = require('util');
const { spawn, execFile } = require('child_process');
const timersPromises = require('timers/promises');
const { check, throws, rejects, section, run } = require('./lib/check');

const { EventEmitter, once, on } = events;
const abortError = { name: 'AbortError', code: 'ABORT_ERR', message: 'The operation was aborted' };
const stamp = (text) => text.replace(/timeStamp: [\d.]+/, 'timeStamp: T');

run(async () => {
  section('Event');
  const event = new Event('ping', { cancelable: true, bubbles: true });
  check('properties', [event.type, event.cancelable, event.bubbles, event.composed, event.defaultPrevented, event.isTrusted,
                       event.eventPhase, event.target, typeof event.timeStamp, event.returnValue],
    ['ping', true, true, false, false, false, 0, null, 'number', true]);
  event.preventDefault();
  check('preventDefault', [event.defaultPrevented, event.returnValue], [true, false]);
  const uncancelable = new Event('x');
  uncancelable.preventDefault();
  check('preventDefault needs cancelable', uncancelable.defaultPrevented, false);
  check('constants', [Event.NONE, Event.CAPTURING_PHASE, Event.AT_TARGET, Event.BUBBLING_PHASE], [0, 1, 2, 3]);
  check('inspect', stamp(util.inspect(new Event('x'))), "Event {\n  type: 'x',\n  defaultPrevented: false,\n  cancelable: false,\n  timeStamp: T\n}");
  check('CustomEvent', [new CustomEvent('c', { detail: { a: 1 } }).detail, new CustomEvent('c').detail, new CustomEvent('c') instanceof Event],
    [{ a: 1 }, null, true]);
  throws('needs a type', () => new Event(), { code: 'ERR_MISSING_ARGS' });

  section('EventTarget');
  const target = new EventTarget();
  const order = [];
  target.addEventListener('o', () => order.push(1), { once: true });
  target.addEventListener('o', { handleEvent: () => order.push(2) });
  const third = (e) => {
    order.push(3);
    e.stopImmediatePropagation();
  };
  target.addEventListener('o', third);
  target.addEventListener('o', third);
  target.addEventListener('o', () => order.push(4));
  target.dispatchEvent(new Event('o'));
  target.dispatchEvent(new Event('o'));
  check('order, once, handleEvent, duplicates, stopImmediatePropagation', order.join(''), '12323');
  let seen;
  target.addEventListener('t', function (e) {
    seen = [this === target, e.target === target, e.currentTarget === target, e.eventPhase, e.composedPath().length];
  });
  const dispatched = new Event('t');
  target.dispatchEvent(dispatched);
  check('during dispatch', seen, [true, true, true, 2, 1]);
  check('after dispatch', [dispatched.currentTarget, dispatched.eventPhase, dispatched.target === target], [null, 0, true]);
  target.addEventListener('c', (e) => e.preventDefault());
  check('dispatchEvent returns !defaultPrevented', [target.dispatchEvent(new Event('c', { cancelable: true })), target.dispatchEvent(new Event('c'))], [false, true]);
  target.addEventListener('p', (e) => e.preventDefault(), { passive: true });
  // Node.js doesn't ignore preventDefault() in passive listeners
  check('passive listeners', target.dispatchEvent(new Event('p', { cancelable: true })), false);
  const removed = () => order.push('removed');
  target.addEventListener('r', removed);
  target.removeEventListener('r', removed);
  target.dispatchEvent(new Event('r'));
  check('removeEventListener', order.includes('removed'), false);
  const listenerError = new Promise((resolve) => process.once('uncaughtException', resolve));
  target.addEventListener('throws', () => { throw new Error('from a listener'); });
  target.addEventListener('throws', () => order.push('after'));
  target.dispatchEvent(new Event('throws'));
  check('a throwing listener doesn\'t stop the others', order.includes('after'), true);
  check('its error is uncaught', (await listenerError).message, 'from a listener');
  throws('dispatchEvent needs an Event', () => target.dispatchEvent({ type: 'x' }), { code: 'ERR_INVALID_ARG_TYPE' });
  check('inspect', util.inspect(new EventTarget()), 'EventTarget {}');

  section('AbortController and AbortSignal');
  const controller = new AbortController();
  const signal = controller.signal;
  const calls = [];
  signal.onabort = (e) => calls.push(['onabort', e.type, e.isTrusted]);
  signal.addEventListener('abort', () => calls.push(['listener', signal.aborted]));
  check('before', [signal.aborted, signal.reason, util.inspect(signal)], [false, undefined, 'AbortSignal { aborted: false }']);
  controller.abort();
  controller.abort('again');
  check('abort events', calls, [['onabort', 'abort', true], ['listener', true]]);
  check('default reason', [signal.reason instanceof DOMException, signal.reason.name, signal.reason.message, signal.reason.code],
    [true, 'AbortError', 'This operation was aborted', 20]);
  throws('throwIfAborted', () => signal.throwIfAborted(), { name: 'AbortError' });
  check('inspect', util.inspect(controller), 'AbortController { signal: AbortSignal { aborted: true } }');
  check('AbortSignal.abort', [AbortSignal.abort().aborted, AbortSignal.abort('why').reason], [true, 'why']);
  const timeout = AbortSignal.timeout(5);
  await timersPromises.setTimeout(30);
  check('AbortSignal.timeout', [timeout.aborted, timeout.reason.name, timeout.reason.code], [true, 'TimeoutError', 23]);
  const first = new AbortController();
  const any = AbortSignal.any([first.signal, new AbortController().signal]);
  first.abort('first');
  check('AbortSignal.any', [any.aborted, any.reason], [true, 'first']);
  check('AbortSignal.any of an aborted signal', AbortSignal.any([AbortSignal.abort('now')]).reason, 'now');
  const withSignal = new AbortController();
  target.addEventListener('s', () => order.push('signal listener'), { signal: withSignal.signal });
  withSignal.abort();
  target.dispatchEvent(new Event('s'));
  check('a listener\'s signal removes it', order.includes('signal listener'), false);
  throws('no public constructor', () => new AbortSignal(), { code: 'ERR_ILLEGAL_CONSTRUCTOR' });

  section('DOMException');
  const domException = new DOMException('m', 'AbortError');
  check('properties', [domException.name, domException.message, domException.code, domException instanceof Error, Object.keys(domException)],
    ['AbortError', 'm', 20, true, []]);
  check('defaults', [new DOMException().name, new DOMException().message, new DOMException('x', 'Custom').code], ['Error', '', 0]);
  check('options', [new DOMException('x', { name: 'DataCloneError', cause: 1 }).code, new DOMException('x', { cause: 1 }).cause], [25, 1]);
  check('inspect', util.inspect(domException).split('\n')[0], 'DOMException [AbortError]: m');
  check('constants', [DOMException.ABORT_ERR, DOMException.prototype.TIMEOUT_ERR], [20, 23]);

  section('timers/promises');
  check('setTimeout', await timersPromises.setTimeout(1, 'value'), 'value');
  check('setImmediate', await timersPromises.setImmediate('now'), 'now');
  check('scheduler.wait', await timersPromises.scheduler.wait(1), undefined);
  const timers = [];
  for await (const value of timersPromises.setInterval(1, 'tick')) {
    timers.push(value);
    if (timers.length === 3) break;
  }
  check('setInterval', timers, ['tick', 'tick', 'tick']);
  check('timers.promises', require('timers').promises, timersPromises);
  const sleeper = new AbortController();
  const sleeping = timersPromises.setTimeout(10000, 'x', { signal: sleeper.signal });
  sleeper.abort('stop');
  await rejects('an abort rejects', sleeping, { ...abortError, cause: 'stop' });
  await rejects('an aborted signal rejects at once', timersPromises.setImmediate('x', { signal: AbortSignal.abort() }), abortError);
  await rejects('options must be an object', timersPromises.setTimeout(1, 'x', 'nope'), { code: 'ERR_INVALID_ARG_TYPE' });

  section('events');
  const emitterTarget = new EventTarget();
  setTimeout(() => emitterTarget.dispatchEvent(new Event('ping')), 1);
  const [received] = await once(emitterTarget, 'ping');
  check('once on an EventTarget', received.type, 'ping');
  const waiting = new AbortController();
  const never = once(new EventEmitter(), 'never', { signal: waiting.signal });
  waiting.abort('why');
  await rejects('once with a signal', never, { ...abortError, cause: 'why' });
  await rejects('once with an aborted signal', once(new EventEmitter(), 'x', { signal: AbortSignal.abort() }), abortError);
  const emitter = new EventEmitter();
  setTimeout(() => {
    emitter.emit('data', 1);
    emitter.emit('data', 2);
    emitter.emit('end');
  }, 1);
  const values = [];
  for await (const [value] of on(emitter, 'data', { close: ['end'] })) values.push(value);
  check('on with close', values, [1, 2]);
  const stopping = new AbortController();
  const iterator = on(new EventEmitter(), 'z', { signal: stopping.signal });
  setTimeout(() => stopping.abort(), 1);
  await rejects('on with a signal', (async () => { for await (const _ of iterator) void _; })(), abortError);
  const listener = () => {};
  emitterTarget.addEventListener('q', listener);
  check('getEventListeners', [events.getEventListeners(emitterTarget, 'q'), events.getEventListeners(emitter, 'none')], [[listener], []]);
  events.setMaxListeners(3, emitterTarget, emitter);
  check('setMaxListeners and getMaxListeners', [events.getMaxListeners(emitterTarget), events.getMaxListeners(emitter)], [3, 3]);
  const abortListened = new AbortController();
  let fired = 0;
  const disposable = events.addAbortListener(abortListened.signal, () => fired++);
  abortListened.abort();
  check('addAbortListener', [fired, typeof disposable[Symbol.dispose]], [1, 'function']);

  section('fs');
  const file = path.join(os.tmpdir(), `frogjs-abort-${process.pid}.txt`);
  await fs.promises.writeFile(file, 'data', { signal: new AbortController().signal });
  check('writeFile with a signal', fs.readFileSync(file, 'utf8'), 'data');
  await rejects('readFile with an aborted signal', fs.promises.readFile(file, { signal: AbortSignal.abort('r') }), { ...abortError, cause: 'r' });
  const reading = new AbortController();
  const read = new Promise((resolve) => fs.readFile(file, { signal: reading.signal }, resolve));
  reading.abort();
  check('readFile aborted while reading', (await read).name, 'AbortError');
  await rejects('fs/promises', require('fs/promises').writeFile(file, 'x', { signal: AbortSignal.abort() }), abortError);
  throws('a signal must be an AbortSignal', () => fs.readFile(file, { signal: 1 }, () => {}), { code: 'ERR_INVALID_ARG_TYPE' });
  fs.unlinkSync(file);

  section('child_process');
  const killer = new AbortController();
  const child = spawn('sleep', ['5'], { signal: killer.signal });
  const childError = new Promise((resolve) => child.on('error', resolve));
  const childExit = new Promise((resolve) => child.on('exit', (code, sig) => resolve([code, sig])));
  setTimeout(() => killer.abort('stop'), 20);
  check('spawn: an AbortError', [(await childError).name, (await childError).cause], ['AbortError', 'stop']);
  check('spawn: killed with SIGTERM', await childExit, [null, 'SIGTERM']);
  const execError = await new Promise((resolve) => execFile('sleep', ['5'], { signal: AbortSignal.abort('pre'), killSignal: 'SIGKILL' }, resolve));
  check('execFile with an aborted signal', [execError.name, execError.cause], ['AbortError', 'pre']);
  throws('a signal must be an AbortSignal', () => spawn('true', [], { signal: 5 }), { code: 'ERR_INVALID_ARG_TYPE' });
});
