#include <v8.h>
#include "frogjs/js_module.h"

using namespace v8;

// The events module is written in JavaScript and follows Node.js semantics:
// listener exceptions propagate to emit()'s caller, and emitting 'error' with
// no listener throws the error.
static const char* kEventsSource = R"JS(
(function () {
'use strict';

const kErrorMonitor = Symbol('events.errorMonitor');
let defaultMaxListeners = 10;

function makeError(Ctor, code, message) {
  const err = new Ctor(message);
  err.code = code;
  return err;
}

function checkListener(listener) {
  if (typeof listener !== 'function') {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
      'The "listener" argument must be of type function. Received ' + describe(listener));
  }
}

function checkMaxListeners(n, name) {
  if (typeof n !== 'number' || n < 0 || Number.isNaN(n)) {
    throw makeError(RangeError, 'ERR_OUT_OF_RANGE',
      `The value of "${name}" is out of range. It must be a non-negative number. Received ${describe(n)}`);
  }
}

function describe(value) {
  if (value === null || value === undefined) return String(value);
  if (typeof value === 'function') return 'function ' + (value.name || '<anonymous>');
  if (typeof value === 'object') return 'an instance of ' + ((value.constructor && value.constructor.name) || 'Object');
  return `type ${typeof value} (${String(value)})`;
}

// Works for objects that inherit from EventEmitter without calling its constructor
function eventsOf(emitter) {
  if (emitter._events === undefined || emitter._events === Object.getPrototypeOf(emitter)._events) {
    emitter._events = Object.create(null);
    emitter._eventsCount = 0;
  }
  return emitter._events;
}

function EventEmitter(options) {
  EventEmitter.init.call(this, options);
}

EventEmitter.init = function init() {
  eventsOf(this);
  this._maxListeners = this._maxListeners || undefined;
};

EventEmitter.prototype._events = undefined;
EventEmitter.prototype._eventsCount = 0;
EventEmitter.prototype._maxListeners = undefined;

EventEmitter.prototype.setMaxListeners = function setMaxListeners(n) {
  checkMaxListeners(n, 'n');
  this._maxListeners = n;
  return this;
};

EventEmitter.prototype.getMaxListeners = function getMaxListeners() {
  return this._maxListeners === undefined ? defaultMaxListeners : this._maxListeners;
};

EventEmitter.prototype.emit = function emit(type, ...args) {
  const events = eventsOf(this);

  if (type === 'error') {
    if (events[kErrorMonitor] !== undefined) this.emit(kErrorMonitor, ...args);
    if (events.error === undefined) {
      const er = args[0];
      if (er instanceof Error) throw er;
      const err = makeError(Error, 'ERR_UNHANDLED_ERROR', 'Unhandled error. (' + describeError(er) + ')');
      err.context = er;
      throw err;
    }
  }

  const list = events[type];
  if (list === undefined) return false;
  // Copy so listeners added or removed while emitting don't affect this emit
  for (const listener of list.slice()) listener.apply(this, args);
  return true;
};

function describeError(er) {
  try {
    return typeof er === 'string' ? `'${er}'` : String(er);
  } catch (e) {
    return 'unknown';
  }
}

function addListener(target, type, listener, prepend) {
  checkListener(listener);
  let events = eventsOf(target);

  // Emit before adding, so a 'newListener' listener can't see itself
  if (events.newListener !== undefined) {
    target.emit('newListener', type, listener.listener ? listener.listener : listener);
    events = eventsOf(target);
  }

  let list = events[type];
  if (list === undefined) {
    list = events[type] = [];
    target._eventsCount++;
  }
  if (prepend) list.unshift(listener);
  else list.push(listener);

  const max = target.getMaxListeners();
  if (max > 0 && list.length > max && !list.warned) {
    list.warned = true;
    const name = (target.constructor && target.constructor.name) || 'EventEmitter';
    const pid = typeof process === 'object' && process ? process.pid : '';
    console.error(`(frogjs:${pid}) MaxListenersExceededWarning: Possible EventEmitter memory leak detected. ` +
      `${list.length} ${String(type)} listeners added to [${name}]. MaxListeners is ${max}. ` +
      'Use emitter.setMaxListeners() to increase limit');
  }
  return target;
}

EventEmitter.prototype.addListener = function addListener_(type, listener) {
  return addListener(this, type, listener, false);
};

EventEmitter.prototype.on = EventEmitter.prototype.addListener;

EventEmitter.prototype.prependListener = function prependListener(type, listener) {
  return addListener(this, type, listener, true);
};

function onceWrapper(target, type, listener) {
  const state = { fired: false, target, type, listener, wrapFn: undefined };
  const wrapped = function (...args) {
    if (state.fired) return undefined;
    state.target.removeListener(state.type, state.wrapFn);
    state.fired = true;
    return state.listener.apply(state.target, args);
  };
  wrapped.listener = listener;
  state.wrapFn = wrapped;
  return wrapped;
}

EventEmitter.prototype.once = function once(type, listener) {
  checkListener(listener);
  this.on(type, onceWrapper(this, type, listener));
  return this;
};

EventEmitter.prototype.prependOnceListener = function prependOnceListener(type, listener) {
  checkListener(listener);
  this.prependListener(type, onceWrapper(this, type, listener));
  return this;
};

EventEmitter.prototype.removeListener = function removeListener(type, listener) {
  checkListener(listener);
  const events = eventsOf(this);
  const list = events[type];
  if (list === undefined) return this;

  // The most recently added match is removed
  let index = -1;
  for (let i = list.length - 1; i >= 0; i--) {
    if (list[i] === listener || list[i].listener === listener) {
      index = i;
      break;
    }
  }
  if (index < 0) return this;

  const removed = list[index];
  if (list.length === 1) {
    delete events[type];
    this._eventsCount--;
  } else {
    // Replace rather than mutate, so an emit() in progress keeps its snapshot semantics
    const next = list.slice(0, index).concat(list.slice(index + 1));
    next.warned = list.warned;
    events[type] = next;
  }

  if (events.removeListener !== undefined) {
    this.emit('removeListener', type, removed.listener || removed);
  }
  return this;
};

EventEmitter.prototype.off = EventEmitter.prototype.removeListener;

EventEmitter.prototype.removeAllListeners = function removeAllListeners(type) {
  const events = eventsOf(this);

  // Fast path: nobody is listening for 'removeListener'
  if (events.removeListener === undefined) {
    if (arguments.length === 0) {
      this._events = Object.create(null);
      this._eventsCount = 0;
    } else if (events[type] !== undefined) {
      delete events[type];
      this._eventsCount--;
    }
    return this;
  }

  if (arguments.length === 0) {
    for (const key of Reflect.ownKeys(events)) {
      if (key !== 'removeListener') this.removeAllListeners(key);
    }
    this.removeAllListeners('removeListener');
    this._events = Object.create(null);
    this._eventsCount = 0;
    return this;
  }

  const list = events[type];
  if (list !== undefined) {
    // LIFO order, like Node.js
    for (let i = list.length - 1; i >= 0; i--) this.removeListener(type, list[i]);
  }
  return this;
};

EventEmitter.prototype.listeners = function listeners(type) {
  const list = eventsOf(this)[type];
  return list === undefined ? [] : list.map((l) => l.listener || l);
};

EventEmitter.prototype.rawListeners = function rawListeners(type) {
  const list = eventsOf(this)[type];
  return list === undefined ? [] : list.slice();
};

EventEmitter.prototype.listenerCount = function listenerCount(type, listener) {
  const list = eventsOf(this)[type];
  if (list === undefined) return 0;
  if (listener === undefined || listener === null) return list.length;
  return list.filter((l) => l === listener || l.listener === listener).length;
};

EventEmitter.prototype.eventNames = function eventNames() {
  return this._eventsCount > 0 ? Reflect.ownKeys(eventsOf(this)) : [];
};

Object.defineProperty(EventEmitter, 'defaultMaxListeners', {
  enumerable: true,
  get() { return defaultMaxListeners; },
  set(n) {
    checkMaxListeners(n, 'defaultMaxListeners');
    defaultMaxListeners = n;
  },
});

// Deprecated in Node.js, kept for compatibility
EventEmitter.listenerCount = function listenerCount(emitter, type) {
  return typeof emitter.listenerCount === 'function'
    ? emitter.listenerCount(type)
    : EventEmitter.prototype.listenerCount.call(emitter, type);
};

const isEventTarget = (value) => value !== null && typeof value === 'object' && typeof value.addEventListener === 'function' &&
  typeof value.dispatchEvent === 'function';
const kEventTargetListeners = (target) => Object.getOwnPropertySymbols(target).find((symbol) => symbol.description === 'listeners');
const kEventTargetMax = (target) => Object.getOwnPropertySymbols(target).find((symbol) => symbol.description === 'maxListeners');

// Node.js's AbortError: what a function rejects with when its signal aborts
class AbortError extends Error {
  constructor(message = 'The operation was aborted', options = undefined) {
    super(message, options);
    this.code = 'ABORT_ERR';
    this.name = 'AbortError';
  }
}

function validateAbortSignal(signal, name) {
  if (signal !== undefined && (signal === null || typeof signal !== 'object' || !('aborted' in signal))) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" property must be an instance of AbortSignal. Received ${describe(signal)}`);
  }
}

function validateOptions(options) {
  if (options !== undefined && (options === null || typeof options !== 'object')) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "options" argument must be of type object. Received ${describe(options)}`);
  }
  return options || {};
}

EventEmitter.getEventListeners = function getEventListeners(emitter, type) {
  if (isEventTarget(emitter) && typeof emitter.listeners !== 'function') {
    const key = kEventTargetListeners(emitter);
    const list = key ? emitter[key].get(String(type)) : undefined;
    return list ? list.map((entry) => entry.listener) : [];
  }
  return emitter.listeners(type);
};

// events.setMaxListeners(n, ...targets): for these emitters and targets, or
// the default for new ones
EventEmitter.setMaxListeners = function setMaxListeners(n = defaultMaxListeners, ...targets) {
  checkMaxListeners(n, 'n');
  if (targets.length === 0) {
    defaultMaxListeners = n;
    return;
  }
  for (const target of targets) {
    if (isEventTarget(target) && typeof target.setMaxListeners !== 'function') {
      const key = kEventTargetMax(target);
      if (key) target[key] = { limit: n, warned: false };
    } else if (target && typeof target.setMaxListeners === 'function') {
      target.setMaxListeners(n);
    } else {
      throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "eventTargets" argument must be an instance of EventEmitter or EventTarget. Received ${describe(target)}`);
    }
  }
};

EventEmitter.getMaxListeners = function getMaxListeners(emitterOrTarget) {
  if (emitterOrTarget && typeof emitterOrTarget.getMaxListeners === 'function') return emitterOrTarget.getMaxListeners();
  if (isEventTarget(emitterOrTarget)) {
    const key = kEventTargetMax(emitterOrTarget);
    return key ? emitterOrTarget[key].limit : defaultMaxListeners;
  }
  throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "emitter" argument must be an instance of EventEmitter or EventTarget. Received ${describe(emitterOrTarget)}`);
};

// events.addAbortListener(signal, listener): listen for the abort, and get a
// disposable that stops listening
EventEmitter.addAbortListener = function addAbortListener(signal, listener) {
  if (signal === undefined) throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "signal" argument must be an instance of AbortSignal. Received undefined');
  validateAbortSignal(signal, 'signal');
  if (typeof listener !== 'function') {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "listener" argument must be of type function. Received ${describe(listener)}`);
  }
  let removed = false;
  if (signal.aborted) {
    queueMicrotask(() => listener());
  } else {
    signal.addEventListener('abort', listener, { once: true });
  }
  const dispose = () => {
    if (removed) return;
    removed = true;
    signal.removeEventListener('abort', listener);
  };
  return { [Symbol.dispose || Symbol.for('nodejs.dispose')]: dispose };
};

// events.once(emitter, name[, { signal }]): a Promise of the next event's
// arguments ([event] for an EventTarget). Rejects if 'error' is emitted
// first (unless waiting for 'error' itself), or when the signal aborts.
EventEmitter.once = function once(emitter, type, options = undefined) {
  const { signal } = validateOptions(options);
  validateAbortSignal(signal, 'options.signal');
  if (signal && signal.aborted) return Promise.reject(new AbortError(undefined, { cause: signal.reason }));
  return new Promise((resolve, reject) => {
    let onAbort;
    const cleanupSignal = () => {
      if (signal) signal.removeEventListener('abort', onAbort);
    };
    if (isEventTarget(emitter) && typeof emitter.once !== 'function') {
      const listener = (event) => {
        cleanupSignal();
        resolve([event]);
      };
      emitter.addEventListener(type, listener, { once: true });
      onAbort = () => {
        emitter.removeEventListener(type, listener);
        reject(new AbortError(undefined, { cause: signal.reason }));
      };
    } else {
      const errorListener = (err) => {
        emitter.removeListener(type, resolver);
        cleanupSignal();
        reject(err);
      };
      const resolver = (...args) => {
        if (type !== 'error') emitter.removeListener('error', errorListener);
        cleanupSignal();
        resolve(args);
      };
      emitter.once(type, resolver);
      if (type !== 'error') emitter.once('error', errorListener);
      onAbort = () => {
        emitter.removeListener(type, resolver);
        emitter.removeListener('error', errorListener);
        reject(new AbortError(undefined, { cause: signal.reason }));
      };
    }
    if (signal) signal.addEventListener('abort', onAbort, { once: true });
  });
};

// events.on(emitter, name[, { signal, close }]): an async iterator of each
// event's arguments. An 'error' event makes the next iteration throw; so does
// the signal aborting. An event named in `close` ends the iteration.
EventEmitter.on = function on(emitter, type, options = undefined) {
  const { signal, close = [] } = validateOptions(options);
  validateAbortSignal(signal, 'options.signal');
  if (signal && signal.aborted) throw new AbortError(undefined, { cause: signal.reason });
  const unconsumed = [];
  const pending = [];
  let error = null;
  let finished = false;
  const target = isEventTarget(emitter) && typeof emitter.on !== 'function';
  const listen = (name, handler) => (target ? emitter.addEventListener(name, handler) : emitter.on(name, handler));
  const unlisten = (name, handler) => (target ? emitter.removeEventListener(name, handler) : emitter.removeListener(name, handler));

  const eventHandler = (...args) => {
    const next = pending.shift();
    if (next) next.resolve({ value: args, done: false });
    else unconsumed.push(args);
  };
  const fail = (err) => {
    finished = true;
    const next = pending.shift();
    if (next) next.reject(err);
    else error = err;
    iterator.return();
  };
  const errorHandler = (err) => fail(err);
  const closeHandler = () => {
    cleanup();
    finished = true;
    for (const p of pending.splice(0)) p.resolve({ value: undefined, done: true });
  };
  const onAbort = () => fail(new AbortError(undefined, { cause: signal.reason }));

  function cleanup() {
    unlisten(type, eventHandler);
    if (!target) unlisten('error', errorHandler);
    for (const name of close) unlisten(name, closeHandler);
    if (signal) signal.removeEventListener('abort', onAbort);
  }

  const iterator = {
    next() {
      const value = unconsumed.shift();
      if (value) return Promise.resolve({ value, done: false });
      if (error) {
        const p = Promise.reject(error);
        error = null;
        return p;
      }
      if (finished) return Promise.resolve({ value: undefined, done: true });
      return new Promise((resolve, reject) => pending.push({ resolve, reject }));
    },
    return() {
      cleanup();
      finished = true;
      for (const p of pending.splice(0)) p.resolve({ value: undefined, done: true });
      return Promise.resolve({ value: undefined, done: true });
    },
    throw(err) {
      error = err;
      cleanup();
    },
    [Symbol.asyncIterator]() { return this; },
  };

  listen(type, eventHandler);
  if (type !== 'error' && !target) listen('error', errorHandler);
  for (const name of close) listen(name, closeHandler);
  if (signal) signal.addEventListener('abort', onAbort, { once: true });
  return iterator;
};

EventEmitter.errorMonitor = kErrorMonitor;
EventEmitter.EventEmitter = EventEmitter;

return EventEmitter;
})
)JS";

// Set up events module: the EventEmitter class, exposed as global `events`
// and returned by require('events')
void SetupEvents(Isolate* isolate, Local<Context> context) {
    Local<Value> events;
    if (!frogjs::RunJsModule(isolate, context, "events", kEventsSource, 0, nullptr).ToLocal(&events)) {
        return;
    }
    context->Global()->Set(context, String::NewFromUtf8(isolate, "events").ToLocalChecked(), events).Check();
}
