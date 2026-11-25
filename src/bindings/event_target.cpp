#include <v8.h>
#include "frogjs/js_module.h"

using namespace v8;

// The web platform's events: DOMException, Event, CustomEvent, EventTarget,
// AbortController and AbortSignal, as globals; and the timers/promises module,
// whose functions take an AbortSignal. EventTarget has no tree here: every
// dispatch is at the target, as in Node.js.
static const char* kEventTargetSource = R"JS(
(function (util, timers, process) {
'use strict';

const inspectCustom = Symbol.for('nodejs.util.inspect.custom');

function codeError(Base, code, message) {
  const err = new Base(message);
  err.code = code;
  return err;
}

function describe(value) {
  if (value === null || value === undefined) return String(value);
  if (typeof value === 'function') return `function ${value.name}`;
  if (typeof value === 'object') return value.constructor && value.constructor.name ? `an instance of ${value.constructor.name}` : util.inspect(value, { depth: -1 });
  return `type ${typeof value} (${util.inspect(value)})`;
}

function invalidArgType(name, expected, value) {
  return codeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}. Received ${describe(value)}`);
}

function missingArgs(...names) {
  const shown = names.map((name) => `"${name}"`).join(' and ');
  return codeError(TypeError, 'ERR_MISSING_ARGS', `The ${shown} argument${names.length > 1 ? 's' : ''} must be specified`);
}

function illegalConstructor() {
  return codeError(TypeError, 'ERR_ILLEGAL_CONSTRUCTOR', 'Illegal constructor');
}

// A class's own instances only: methods check their receiver
function invalidThis(type) {
  return codeError(TypeError, 'ERR_INVALID_THIS', `Value of "this" must be of type ${type}`);
}

function makeEnumerable(Class, names) {
  for (const name of names) {
    const descriptor = Object.getOwnPropertyDescriptor(Class.prototype, name);
    descriptor.enumerable = true;
    Object.defineProperty(Class.prototype, name, descriptor);
  }
}

// ---- DOMException ----

const legacyCodes = {
  IndexSizeError: 1, HierarchyRequestError: 3, WrongDocumentError: 4, InvalidCharacterError: 5,
  NoModificationAllowedError: 7, NotFoundError: 8, NotSupportedError: 9, InUseAttributeError: 10,
  InvalidStateError: 11, SyntaxError: 12, InvalidModificationError: 13, NamespaceError: 14,
  InvalidAccessError: 15, TypeMismatchError: 17, SecurityError: 18, NetworkError: 19, AbortError: 20,
  URLMismatchError: 21, QuotaExceededError: 22, TimeoutError: 23, InvalidNodeTypeError: 24, DataCloneError: 25,
};
const legacyConstants = {
  INDEX_SIZE_ERR: 1, DOMSTRING_SIZE_ERR: 2, HIERARCHY_REQUEST_ERR: 3, WRONG_DOCUMENT_ERR: 4, INVALID_CHARACTER_ERR: 5,
  NO_DATA_ALLOWED_ERR: 6, NO_MODIFICATION_ALLOWED_ERR: 7, NOT_FOUND_ERR: 8, NOT_SUPPORTED_ERR: 9, INUSE_ATTRIBUTE_ERR: 10,
  INVALID_STATE_ERR: 11, SYNTAX_ERR: 12, INVALID_MODIFICATION_ERR: 13, NAMESPACE_ERR: 14, INVALID_ACCESS_ERR: 15,
  VALIDATION_ERR: 16, TYPE_MISMATCH_ERR: 17, SECURITY_ERR: 18, NETWORK_ERR: 19, ABORT_ERR: 20, URL_MISMATCH_ERR: 21,
  QUOTA_EXCEEDED_ERR: 22, TIMEOUT_ERR: 23, INVALID_NODE_TYPE_ERR: 24, DATA_CLONE_ERR: 25,
};

const kDOMException = Symbol('DOMException');

class DOMException {
  constructor(message = '', options = 'Error') {
    let name = 'Error';
    if (options !== null && typeof options === 'object') {
      name = 'name' in options ? String(options.name) : 'Error';
      if ('cause' in options) {
        Object.defineProperty(this, 'cause', { value: options.cause, configurable: true, writable: true, enumerable: false });
      }
    } else {
      name = String(options);
    }
    Object.defineProperty(this, kDOMException, { value: { message: String(message), name } });
    // The stack starts with "<name>: <message>", as for errors
    if (Error.captureStackTrace) Error.captureStackTrace(this);
  }

  get name() {
    if (!this || !this[kDOMException]) throw invalidThis('DOMException');
    return this[kDOMException].name;
  }

  get message() {
    if (!this || !this[kDOMException]) throw invalidThis('DOMException');
    return this[kDOMException].message;
  }

  get code() {
    if (!this || !this[kDOMException]) throw invalidThis('DOMException');
    return legacyCodes[this[kDOMException].name] || 0;
  }
}
Object.setPrototypeOf(DOMException.prototype, Error.prototype);
makeEnumerable(DOMException, ['name', 'message', 'code']);
Object.defineProperty(DOMException.prototype, Symbol.toStringTag, { value: 'DOMException', configurable: true });
for (const [name, value] of Object.entries(legacyConstants)) {
  Object.defineProperty(DOMException, name, { value, enumerable: true });
  Object.defineProperty(DOMException.prototype, name, { value, enumerable: true });
}

// Node.js's AbortError, which functions that take a signal reject with
class AbortError extends Error {
  constructor(message = 'The operation was aborted', options = undefined) {
    super(message, options);
    this.code = 'ABORT_ERR';
    this.name = 'AbortError';
  }
}

// ---- Event ----

const kEvent = Symbol('event');
const NONE = 0, CAPTURING_PHASE = 1, AT_TARGET = 2, BUBBLING_PHASE = 3;

function eventState(event) {
  if (event === null || typeof event !== 'object' || !event[kEvent]) throw invalidThis('Event');
  return event[kEvent];
}

class Event {
  constructor(type = undefined, options = undefined) {
    if (arguments.length === 0) throw missingArgs('type');
    if (options !== undefined && options !== null && typeof options !== 'object' && typeof options !== 'function') {
      throw invalidArgType('options', 'of type object', options);
    }
    Object.defineProperty(this, kEvent, {
      value: {
        type: String(type),
        bubbles: Boolean(options && options.bubbles),
        cancelable: Boolean(options && options.cancelable),
        composed: Boolean(options && options.composed),
        defaultPrevented: false,
        timeStamp: performance.now(),
        propagationStopped: false,
        immediateStopped: false,
        target: null,
        currentTarget: null,
        eventPhase: NONE,
        isTrusted: false,
        dispatching: false,
      },
    });
    // isTrusted is an own property in browsers and Node.js
    Object.defineProperty(this, 'isTrusted', { get: isTrusted, enumerable: true, configurable: false });
  }

  get type() { return eventState(this).type; }
  get bubbles() { return eventState(this).bubbles; }
  get cancelable() { return eventState(this).cancelable; }
  get composed() { return eventState(this).composed; }
  get defaultPrevented() {
    const state = eventState(this);
    return state.cancelable && state.defaultPrevented;
  }
  get timeStamp() { return eventState(this).timeStamp; }
  get target() { return eventState(this).target; }
  get currentTarget() { return eventState(this).currentTarget; }
  get srcElement() { return eventState(this).target; }
  get eventPhase() { return eventState(this).eventPhase; }
  get returnValue() { return !this.defaultPrevented; }
  set returnValue(value) {
    if (!value) this.preventDefault();
  }
  get cancelBubble() { return eventState(this).propagationStopped; }
  set cancelBubble(value) {
    if (value) eventState(this).propagationStopped = true;
  }

  composedPath() {
    const state = eventState(this);
    return state.dispatching && state.currentTarget ? [state.currentTarget] : [];
  }

  preventDefault() {
    const state = eventState(this);
    // As in Node.js, also in a passive listener
    if (state.cancelable) state.defaultPrevented = true;
  }

  stopPropagation() {
    eventState(this).propagationStopped = true;
  }

  stopImmediatePropagation() {
    const state = eventState(this);
    state.propagationStopped = true;
    state.immediateStopped = true;
  }

  [inspectCustom](depth, options, inspect) {
    const name = this.constructor.name;
    if (depth < 0) return name;
    const show = typeof inspect === 'function' ? inspect : util.inspect;
    const fields = { type: this.type, defaultPrevented: this.defaultPrevented, cancelable: this.cancelable, timeStamp: this.timeStamp };
    return `${name} ${show(fields, { ...options, depth: options && options.depth != null ? options.depth - 1 : null })}`;
  }
}

function isTrusted() {
  return eventState(this).isTrusted;
}

makeEnumerable(Event, ['type', 'bubbles', 'cancelable', 'composed', 'defaultPrevented', 'timeStamp', 'target', 'currentTarget',
  'srcElement', 'eventPhase', 'returnValue', 'cancelBubble', 'composedPath', 'preventDefault', 'stopPropagation', 'stopImmediatePropagation']);
Object.defineProperty(Event.prototype, Symbol.toStringTag, { value: 'Event', configurable: true });
for (const [name, value] of Object.entries({ NONE, CAPTURING_PHASE, AT_TARGET, BUBBLING_PHASE })) {
  Object.defineProperty(Event, name, { value, enumerable: true });
  Object.defineProperty(Event.prototype, name, { value, enumerable: true });
}

const kDetail = Symbol('detail');

class CustomEvent extends Event {
  constructor(type = undefined, options = undefined) {
    if (arguments.length === 0) throw missingArgs('type');
    super(type, options);
    Object.defineProperty(this, kDetail, { value: options && options.detail !== undefined ? options.detail : null });
  }

  get detail() {
    if (!this || !(kDetail in this)) throw invalidThis('CustomEvent');
    return this[kDetail];
  }
}
makeEnumerable(CustomEvent, ['detail']);
Object.defineProperty(CustomEvent.prototype, Symbol.toStringTag, { value: 'CustomEvent', configurable: true });

// ---- EventTarget ----

const kListeners = Symbol('listeners');
const kMaxListeners = Symbol('maxListeners');
const kHandlers = Symbol('handlers');

function listenersOf(target) {
  if (target === null || typeof target !== 'object' || !target[kListeners]) throw invalidThis('EventTarget');
  return target[kListeners];
}

function normalizeOptions(options) {
  if (typeof options === 'boolean') return { capture: options, once: false, passive: false, signal: undefined };
  if (options === null || options === undefined) return { capture: false, once: false, passive: false, signal: undefined };
  if (typeof options !== 'object') throw invalidArgType('options', 'of type object', options);
  const signal = options.signal;
  if (signal !== undefined && !(signal instanceof AbortSignal)) throw invalidArgType('options.signal', 'an instance of AbortSignal', signal);
  return { capture: Boolean(options.capture), once: Boolean(options.once), passive: Boolean(options.passive), signal };
}

// An error from a listener is uncaught, after the dispatch (as in Node.js)
function reportListenerError(err) {
  process.nextTick(() => {
    throw err;
  });
}

class EventTarget {
  constructor() {
    Object.defineProperty(this, kListeners, { value: new Map() });
    Object.defineProperty(this, kMaxListeners, { value: { limit: 10, warned: false }, writable: true });
  }

  addEventListener(type = undefined, listener = undefined, options = undefined) {
    const listeners = listenersOf(this);
    if (arguments.length < 2) throw missingArgs('type', 'listener');
    const { capture, once, passive, signal } = normalizeOptions(options);
    if (listener === null || listener === undefined) {
      process.emitWarning(`addEventListener called with ${listener} which has no effect.`, 'AddEventListenerArgumentTypeWarning');
      return;
    }
    if (typeof listener !== 'function' && typeof listener !== 'object') {
      throw invalidArgType('listener', 'an instance of EventListener', listener);
    }
    if (signal && signal.aborted) return;
    type = String(type);
    let list = listeners.get(type);
    if (!list) {
      list = [];
      listeners.set(type, list);
    }
    if (list.some((entry) => entry.listener === listener && entry.capture === capture)) return;
    const entry = { listener, capture, once, passive, removed: false, signal, onAbort: null };
    list.push(entry);
    if (signal) {
      entry.onAbort = () => this.removeEventListener(type, listener, { capture });
      signal.addEventListener('abort', entry.onAbort, { once: true });
    }
    const limit = this[kMaxListeners].limit;
    if (limit > 0 && list.length > limit && !this[kMaxListeners].warned) {
      this[kMaxListeners].warned = true;
      const warning = new Error(`Possible EventTarget memory leak detected. ${list.length} ${type} listeners added to ${util.inspect(this, { depth: -1 })}. ` +
        `MaxListeners is ${limit}. Use events.setMaxListeners() to increase limit`);
      warning.name = 'MaxListenersExceededWarning';
      warning.target = this;
      warning.type = type;
      warning.count = list.length;
      process.emitWarning(warning);
    }
  }

  removeEventListener(type = undefined, listener = undefined, options = undefined) {
    const listeners = listenersOf(this);
    if (arguments.length < 2) throw missingArgs('type', 'listener');
    const capture = typeof options === 'boolean' ? options : Boolean(options && options.capture);
    type = String(type);
    const list = listeners.get(type);
    if (!list) return;
    const index = list.findIndex((entry) => entry.listener === listener && entry.capture === capture);
    if (index < 0) return;
    const [entry] = list.splice(index, 1);
    entry.removed = true;
    if (entry.signal && entry.onAbort) entry.signal.removeEventListener('abort', entry.onAbort);
    if (list.length === 0) listeners.delete(type);
  }

  dispatchEvent(event = undefined) {
    listenersOf(this);
    if (!(event instanceof Event)) throw invalidArgType('event', 'an instance of Event', event);
    const state = event[kEvent];
    if (state.dispatching) throw new DOMException('The event is already being dispatched', 'InvalidStateError');
    state.dispatching = true;
    state.target = this;
    state.currentTarget = this;
    state.eventPhase = AT_TARGET;
    const list = this[kListeners].get(state.type);
    if (list) {
      for (const entry of list.slice()) {
        if (entry.removed) continue;
        if (entry.once) this.removeEventListener(state.type, entry.listener, { capture: entry.capture });
        try {
          let result;
          if (typeof entry.listener === 'function') {
            result = entry.listener.call(this, event);
          } else if (typeof entry.listener.handleEvent === 'function') {
            result = entry.listener.handleEvent(event);
          } else {
            throw invalidArgType('listener.handleEvent', 'of type function', entry.listener.handleEvent);
          }
          if (result !== undefined && result !== null && typeof result.then === 'function') {
            result.then(undefined, reportListenerError);
          }
        } catch (err) {
          reportListenerError(err);
        }
        if (state.immediateStopped) break;
      }
    }
    state.dispatching = false;
    state.currentTarget = null;
    state.eventPhase = NONE;
    return !event.defaultPrevented;
  }

  [inspectCustom](depth, options, inspect) {
    const name = this.constructor.name;
    if (depth < 0) return name;
    return `${name} {}`;
  }
}
makeEnumerable(EventTarget, ['addEventListener', 'removeEventListener', 'dispatchEvent']);
Object.defineProperty(EventTarget.prototype, Symbol.toStringTag, { value: 'EventTarget', configurable: true });

// An on<event> attribute (onabort): a listener added the first time one is set
function defineEventHandler(Class, type) {
  Object.defineProperty(Class.prototype, `on${type}`, {
    enumerable: true,
    configurable: true,
    get() {
      const handlers = this[kHandlers];
      return handlers && handlers[type] ? handlers[type].handler : null;
    },
    set(value) {
      if (!this[kHandlers]) Object.defineProperty(this, kHandlers, { value: Object.create(null) });
      let slot = this[kHandlers][type];
      if (!slot) {
        slot = { handler: null };
        slot.wrapper = function (event) {
          if (typeof slot.handler === 'function') return slot.handler.call(this, event);
          return undefined;
        };
        this[kHandlers][type] = slot;
      }
      const handler = typeof value === 'function' || (typeof value === 'object' && value !== null) ? value : null;
      if (handler && !slot.handler) this.addEventListener(type, slot.wrapper);
      else if (!handler && slot.handler) this.removeEventListener(type, slot.wrapper);
      slot.handler = handler;
    },
  });
}

// ---- AbortController and AbortSignal ----

const kCreate = Symbol('create');
const kAbort = Symbol('abort');

function signalState(signal) {
  if (signal === null || typeof signal !== 'object' || !signal[kAbort]) throw invalidThis('AbortSignal');
  return signal[kAbort];
}

function abortSignal(signal, reason) {
  const state = signal[kAbort];
  if (state.aborted) return;
  state.aborted = true;
  state.reason = reason;
  const event = new Event('abort');
  event[kEvent].isTrusted = true;
  signal.dispatchEvent(event);
}

class AbortSignal extends EventTarget {
  constructor(key = undefined) {
    if (key !== kCreate) throw illegalConstructor();
    super();
    Object.defineProperty(this, kAbort, { value: { aborted: false, reason: undefined } });
  }

  get aborted() {
    return signalState(this).aborted;
  }

  get reason() {
    return signalState(this).reason;
  }

  throwIfAborted() {
    const state = signalState(this);
    if (state.aborted) throw state.reason;
  }

  static abort(reason = new DOMException('This operation was aborted', 'AbortError')) {
    const signal = new AbortSignal(kCreate);
    signal[kAbort].aborted = true;
    signal[kAbort].reason = reason;
    return signal;
  }

  static timeout(delay) {
    if (!Number.isInteger(delay) || delay < 0 || delay > 4294967295) {
      if (typeof delay !== 'number') throw invalidArgType('delay', 'of type number', delay);
      throw codeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "delay" is out of range. It must be >= 0 && <= 4294967295. Received ${delay}`);
    }
    const signal = new AbortSignal(kCreate);
    // The timer doesn't keep the process alive
    const timer = timers.setTimeout(() => {
      abortSignal(signal, new DOMException('The operation was aborted due to timeout', 'TimeoutError'));
    }, delay);
    if (timer && typeof timer.unref === 'function') timer.unref();
    return signal;
  }

  static any(signals) {
    if (signals === null || typeof signals !== 'object' || typeof signals[Symbol.iterator] !== 'function') {
      throw invalidArgType('signals', 'an instance of Array', signals);
    }
    const sources = Array.from(signals);
    sources.forEach((source, i) => {
      if (!(source instanceof AbortSignal)) throw invalidArgType(`signals[${i}]`, 'an instance of AbortSignal', source);
    });
    const signal = new AbortSignal(kCreate);
    const aborted = sources.find((source) => source.aborted);
    if (aborted) {
      signal[kAbort].aborted = true;
      signal[kAbort].reason = aborted.reason;
      return signal;
    }
    const onAbort = (event) => {
      for (const source of sources) source.removeEventListener('abort', onAbort);
      abortSignal(signal, event.target.reason);
    };
    for (const source of sources) source.addEventListener('abort', onAbort);
    return signal;
  }

  [inspectCustom](depth, options, inspect) {
    if (depth < 0) return 'AbortSignal';
    const show = typeof inspect === 'function' ? inspect : util.inspect;
    return `AbortSignal ${show({ aborted: this.aborted }, options)}`;
  }
}
makeEnumerable(AbortSignal, ['aborted', 'reason', 'throwIfAborted']);
defineEventHandler(AbortSignal, 'abort');
Object.defineProperty(AbortSignal.prototype, Symbol.toStringTag, { value: 'AbortSignal', configurable: true });

const kSignal = Symbol('signal');

class AbortController {
  constructor() {
    Object.defineProperty(this, kSignal, { value: new AbortSignal(kCreate) });
  }

  get signal() {
    if (!this || !this[kSignal]) throw invalidThis('AbortController');
    return this[kSignal];
  }

  abort(reason = new DOMException('This operation was aborted', 'AbortError')) {
    if (!this || !this[kSignal]) throw invalidThis('AbortController');
    abortSignal(this[kSignal], reason);
  }

  [inspectCustom](depth, options, inspect) {
    if (depth < 0) return 'AbortController';
    const show = typeof inspect === 'function' ? inspect : util.inspect;
    return `AbortController ${show({ signal: this.signal }, options)}`;
  }
}
makeEnumerable(AbortController, ['signal', 'abort']);
Object.defineProperty(AbortController.prototype, Symbol.toStringTag, { value: 'AbortController', configurable: true });

// ---- timers/promises ----

function validateSignal(signal, name = 'options.signal') {
  if (signal !== undefined && (signal === null || typeof signal !== 'object' || !('aborted' in signal))) {
    throw invalidArgType(name, 'an instance of AbortSignal', signal);
  }
}

function validateTimerOptions(options) {
  if (options === null || typeof options !== 'object') throw invalidArgType('options', 'of type object', options);
  const { signal, ref = true } = options;
  validateSignal(signal);
  if (typeof ref !== 'boolean') throw invalidArgType('options.ref', 'of type boolean', ref);
  return { signal, ref };
}

// A promise of a timer that a signal can cancel
function cancellable(start, clear, value, options) {
  const { signal, ref } = validateTimerOptions(options);
  if (signal && signal.aborted) return Promise.reject(new AbortError(undefined, { cause: signal.reason }));
  return new Promise((resolve, reject) => {
    let onAbort;
    const timer = start(() => {
      if (signal) signal.removeEventListener('abort', onAbort);
      resolve(value);
    });
    if (!ref && timer && typeof timer.unref === 'function') timer.unref();
    if (signal) {
      onAbort = () => {
        clear(timer);
        reject(new AbortError(undefined, { cause: signal.reason }));
      };
      signal.addEventListener('abort', onAbort, { once: true });
    }
  });
}

function setTimeoutPromise(delay = 1, value = undefined, options = {}) {
  try {
    return cancellable((done) => timers.setTimeout(done, delay), timers.clearTimeout, value, options);
  } catch (err) {
    return Promise.reject(err);
  }
}

function setImmediatePromise(value = undefined, options = {}) {
  try {
    return cancellable((done) => timers.setImmediate(done), timers.clearImmediate, value, options);
  } catch (err) {
    return Promise.reject(err);
  }
}

async function* setIntervalPromise(delay = 1, value = undefined, options = {}) {
  const { signal, ref } = validateTimerOptions(options);
  if (signal && signal.aborted) throw new AbortError(undefined, { cause: signal.reason });
  let pending = 0;
  let wake = null;
  const interval = timers.setInterval(() => {
    pending++;
    if (wake) {
      wake();
      wake = null;
    }
  }, delay);
  if (!ref && interval && typeof interval.unref === 'function') interval.unref();
  let onAbort;
  if (signal) {
    onAbort = () => {
      if (wake) {
        wake();
        wake = null;
      }
    };
    signal.addEventListener('abort', onAbort, { once: true });
  }
  try {
    for (;;) {
      if (pending === 0) await new Promise((resolve) => { wake = resolve; });
      if (signal && signal.aborted) throw new AbortError(undefined, { cause: signal.reason });
      for (; pending > 0; pending--) yield value;
    }
  } finally {
    timers.clearInterval(interval);
    if (signal) signal.removeEventListener('abort', onAbort);
  }
}

const scheduler = {
  wait(delay, options = undefined) {
    return setTimeoutPromise(delay, undefined, options === undefined ? {} : options);
  },
  yield() {
    return setImmediatePromise();
  },
};

const timersPromises = { setTimeout: setTimeoutPromise, setImmediate: setImmediatePromise, setInterval: setIntervalPromise, scheduler };

// ---- atob and btoa ----

const base64Alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const base64Values = new Map([...base64Alphabet].map((ch, i) => [ch, i]));

// Forgiving base64: whitespace is ignored and padding is optional. The
// checks, and which error each gives, are Node.js's.
function atob(input = undefined) {
  if (arguments.length === 0) throw missingArgs('input');
  input = `${input}`;
  let count = 0;
  let equals = 0;
  for (let i = 0; i < input.length; i++) {
    const ch = input[i];
    if (ch === '\t' || ch === '\n' || ch === '\f' || ch === '\r' || ch === ' ') continue;
    if (ch === '=') {
      count++;
      equals++;
      if (equals > 2) throw new DOMException('Invalid character', 'InvalidCharacterError');
    } else if (base64Values.has(ch)) {
      count++;
      // '=' only at the end
      if (equals) throw new DOMException('Invalid character', 'InvalidCharacterError');
    } else {
      throw new DOMException('Invalid character', 'InvalidCharacterError');
    }
  }
  let remainder = count % 4;
  if (!remainder) remainder = (count - equals) % 4;
  else if (equals) throw new DOMException('Invalid character', 'InvalidCharacterError');
  if (remainder === 1) throw new DOMException('The string to be decoded is not correctly encoded.', 'InvalidCharacterError');
  let out = '';
  let buffer = 0;
  let bits = 0;
  for (const ch of input) {
    const value = base64Values.get(ch);
    if (value === undefined) continue;
    buffer = ((buffer << 6) | value) & 0xffffff;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += String.fromCharCode((buffer >> bits) & 0xff);
    }
  }
  return out;
}

function btoa(input = undefined) {
  if (arguments.length === 0) throw missingArgs('input');
  const data = String(input);
  let out = '';
  for (let i = 0; i < data.length; i += 3) {
    const codes = [data.charCodeAt(i), data.charCodeAt(i + 1), data.charCodeAt(i + 2)];
    for (let j = 0; j < 3 && i + j < data.length; j++) {
      if (codes[j] > 0xff) throw new DOMException('Invalid character', 'InvalidCharacterError');
    }
    const [a, b, c] = codes;
    out += base64Alphabet[a >> 2];
    out += base64Alphabet[((a & 3) << 4) | (i + 1 < data.length ? b >> 4 : 0)];
    out += i + 1 < data.length ? base64Alphabet[((b & 15) << 2) | (i + 2 < data.length ? c >> 6 : 0)] : '=';
    out += i + 2 < data.length ? base64Alphabet[c & 63] : '=';
  }
  return out;
}

return { DOMException, Event, CustomEvent, EventTarget, AbortController, AbortSignal, atob, btoa, timersPromises };
})
)JS";

// Set up the event globals and timers/promises (requires SetupUtil, SetupTimers
// and SetupProcess first)
void SetupEventTarget(Isolate* isolate, Local<Context> context) {
    Local<Object> global = context->Global();
    Local<Object> timers = Object::New(isolate);
    for (const char* name : {"setTimeout", "clearTimeout", "setInterval", "clearInterval", "setImmediate", "clearImmediate"}) {
        Local<String> key = String::NewFromUtf8(isolate, name).ToLocalChecked();
        timers->Set(context, key, global->Get(context, key).ToLocalChecked()).Check();
    }
    Local<Value> argv[3] = {frogjs::GetBuiltin(isolate, context, "util"), timers, frogjs::GetBuiltin(isolate, context, "process")};
    Local<Value> result;
    if (!frogjs::RunJsModule(isolate, context, "event_target", kEventTargetSource, 3, argv).ToLocal(&result)) return;
    Local<Object> exports = result.As<Object>();
    for (const char* name : {"DOMException", "Event", "CustomEvent", "EventTarget", "AbortController", "AbortSignal", "atob", "btoa"}) {
        Local<String> key = String::NewFromUtf8(isolate, name).ToLocalChecked();
        global->Set(context, key, exports->Get(context, key).ToLocalChecked()).Check();
    }
    Local<Value> timers_promises = exports->Get(context, String::NewFromUtf8Literal(isolate, "timersPromises")).ToLocalChecked();
    frogjs::RegisterBuiltin(isolate, context, "timers/promises", timers_promises);
}
