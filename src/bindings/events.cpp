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

EventEmitter.getEventListeners = function getEventListeners(emitter, type) {
  return emitter.listeners(type);
};

// events.once(emitter, name): a Promise of the next event's arguments.
// Rejects if 'error' is emitted first (unless waiting for 'error' itself).
EventEmitter.once = function once(emitter, type) {
  return new Promise((resolve, reject) => {
    const errorListener = (err) => {
      emitter.removeListener(type, resolver);
      reject(err);
    };
    const resolver = (...args) => {
      if (type !== 'error') emitter.removeListener('error', errorListener);
      resolve(args);
    };
    emitter.once(type, resolver);
    if (type !== 'error') emitter.once('error', errorListener);
  });
};

// events.on(emitter, name): an async iterator of each event's arguments.
// An 'error' event makes the next iteration throw.
EventEmitter.on = function on(emitter, type) {
  const unconsumed = [];
  const pending = [];
  let error = null;
  let finished = false;

  const eventHandler = (...args) => {
    const next = pending.shift();
    if (next) next.resolve({ value: args, done: false });
    else unconsumed.push(args);
  };
  const errorHandler = (err) => {
    finished = true;
    const next = pending.shift();
    if (next) next.reject(err);
    else error = err;
    iterator.return();
  };

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
      emitter.removeListener(type, eventHandler);
      emitter.removeListener('error', errorHandler);
      finished = true;
      for (const p of pending.splice(0)) p.resolve({ value: undefined, done: true });
      return Promise.resolve({ value: undefined, done: true });
    },
    throw(err) {
      error = err;
      emitter.removeListener(type, eventHandler);
      emitter.removeListener('error', errorHandler);
      return Promise.reject(err);
    },
    [Symbol.asyncIterator]() { return this; },
  };

  emitter.on(type, eventHandler);
  if (type !== 'error') emitter.on('error', errorHandler);
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
