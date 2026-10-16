#include <v8.h>
#include "frogjs/buffer.h"
#include "frogjs/js_module.h"

using namespace v8;

// The stream module is written in JavaScript and follows the Node.js stream
// algorithms (buffering, flowing/paused modes, highWaterMark backpressure,
// destroy/autoDestroy). Differences from Node.js:
// - Strings are not converted to Buffers: they reach _write() as strings, and
//   their length (for highWaterMark) is counted in characters, not bytes.
// - setEncoding() supports utf8, latin1/binary, ascii and hex.
static const char* kStreamSource = R"JS(
(function (EventEmitter, Buffer, binding) {
'use strict';

const kDefaultHighWaterMark = 16384;
const kDefaultObjectHighWaterMark = 16;
const kTransformCallback = Symbol('kTransformCallback');

function nop() {}

function reportError(err) {
  console.error('Uncaught ' + ((err && err.stack) || err));
}

// Run fn(...args) after the current operation (V8 runs microtasks after each callback)
function nextTick(fn, ...args) {
  Promise.resolve().then(() => {
    try { fn(...args); } catch (err) { reportError(err); }
  });
}

function makeError(Ctor, code, message) {
  const err = new Ctor(message);
  err.code = code;
  return err;
}

const errors = {
  methodNotImplemented: (method) => makeError(Error, 'ERR_METHOD_NOT_IMPLEMENTED', `The ${method} method is not implemented`),
  pushAfterEOF: () => makeError(Error, 'ERR_STREAM_PUSH_AFTER_EOF', 'stream.push() after EOF'),
  unshiftAfterEnd: () => makeError(Error, 'ERR_STREAM_UNSHIFT_AFTER_END_EVENT', 'stream.unshift() after end event'),
  writeAfterEnd: () => makeError(Error, 'ERR_STREAM_WRITE_AFTER_END', 'write after end'),
  destroyed: (method) => makeError(Error, 'ERR_STREAM_DESTROYED', `Cannot call ${method} after a stream was destroyed`),
  alreadyFinished: (method) => makeError(Error, 'ERR_STREAM_ALREADY_FINISHED', `Cannot call ${method} after a stream was finished`),
  nullValues: () => makeError(TypeError, 'ERR_STREAM_NULL_VALUES', 'May not write null values to stream'),
  multipleCallback: () => makeError(Error, 'ERR_MULTIPLE_CALLBACK', 'Callback called multiple times'),
  prematureClose: () => makeError(Error, 'ERR_STREAM_PREMATURE_CLOSE', 'Premature close'),
  invalidChunk: (chunk) => makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
    'The "chunk" argument must be of type string or Buffer. Received ' + (chunk === null ? 'null' : typeof chunk)),
  invalidArg: (name, expected) => makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}`),
};

// ---------------------------------------------------------------------------
// String decoding (Buffers and byte strings -> text, across chunk boundaries)

const ENCODINGS = {
  utf8: 'utf8', 'utf-8': 'utf8', latin1: 'latin1', binary: 'latin1', ascii: 'ascii', hex: 'hex',
};

function normalizeEncoding(encoding) {
  if (encoding === undefined || encoding === null) return 'utf8';
  const normalized = ENCODINGS[String(encoding).toLowerCase()];
  if (!normalized) throw makeError(TypeError, 'ERR_UNKNOWN_ENCODING', `Unknown encoding: ${encoding}`);
  return normalized;
}

// Index where a trailing, incomplete UTF-8 sequence starts (length if none)
function incompleteUtf8Tail(bytes) {
  const len = bytes.length;
  for (let i = len - 1; i >= 0 && i >= len - 3; i--) {
    const c = bytes.charCodeAt(i);
    if ((c & 0xc0) === 0x80) continue;
    const need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
    return len - i < need ? i : len;
  }
  return len;
}

class StringDecoder {
  constructor(encoding) {
    this.encoding = normalizeEncoding(encoding);
    this.pending = '';
  }

  write(chunk) {
    if (typeof chunk === 'string' && this.encoding === 'utf8' && this.pending === '') return chunk;
    const bytes = binding.toByteString(chunk);
    switch (this.encoding) {
      case 'latin1': return bytes;
      case 'ascii': return bytes.replace(/[\x80-\xff]/g, (c) => String.fromCharCode(c.charCodeAt(0) & 0x7f));
      case 'hex': {
        let out = '';
        for (let i = 0; i < bytes.length; i++) out += (bytes.charCodeAt(i) | 0x100).toString(16).slice(1);
        return out;
      }
      default: {
        const all = this.pending + bytes;
        const cut = incompleteUtf8Tail(all);
        this.pending = all.slice(cut);
        return cut > 0 ? binding.byteStringToUtf8(all.slice(0, cut)) : '';
      }
    }
  }

  end(chunk) {
    let out = chunk === undefined ? '' : this.write(chunk);
    if (this.pending) out += binding.byteStringToUtf8(this.pending);
    this.pending = '';
    return out;
  }
}

// ---------------------------------------------------------------------------
// Shared helpers

function getHighWaterMark(options, duplexKey, isDuplex, objectMode) {
  let hwm = options ? options.highWaterMark : undefined;
  if ((hwm === undefined || hwm === null) && isDuplex && options) hwm = options[duplexKey];
  if (hwm === undefined || hwm === null) return objectMode ? kDefaultObjectHighWaterMark : kDefaultHighWaterMark;
  if (!Number.isInteger(hwm) || hwm < 0) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The property 'options.highWaterMark' is invalid. Received ${hwm}`);
  }
  return hwm;
}

function isValidChunk(chunk) {
  return typeof chunk === 'string' || Buffer.isBuffer(chunk);
}

// Join string and Buffer chunks: a string if all are strings, else a Buffer
function joinChunks(chunks) {
  if (chunks.length === 1) return chunks[0];
  if (chunks.every((c) => typeof c === 'string')) return chunks.join('');
  return Buffer.concat(chunks.map((c) => typeof c === 'string' ? Buffer.from(c) : c));
}

function isReadableNodeStream(obj) {
  return !!(obj && typeof obj.pipe === 'function' && typeof obj.on === 'function' &&
    (!obj._writableState || (obj._readableState && obj._readableState.readable !== false)));
}

function isWritableNodeStream(obj) {
  return !!(obj && typeof obj.write === 'function' && typeof obj.on === 'function' &&
    (!obj._readableState || (obj._writableState && obj._writableState.writable !== false)));
}

function isNodeStream(obj) {
  return !!(obj && (obj._readableState || obj._writableState ||
    (typeof obj.on === 'function' && (typeof obj.write === 'function' || typeof obj.pipe === 'function'))));
}

// ---------------------------------------------------------------------------
// destroy() shared by Readable, Writable and Duplex

function destroy(err, cb) {
  const r = this._readableState;
  const w = this._writableState;

  if ((w && w.destroyed) || (r && r.destroyed)) {
    if (typeof cb === 'function') cb();
    return this;
  }

  setErrored(err, w, r);
  if (w) {
    w.destroyed = true;
    // Pending writes will never happen; fail their callbacks
    if (w.buffered.length > 0) nextTick(errorBuffer, w);
  }
  if (r) r.destroyed = true;

  let called = false;
  const onDestroy = (er) => {
    if (called) {
      reportError(errors.multipleCallback());
      return;
    }
    called = true;
    setErrored(er, w, r);
    if (w) w.closed = true;
    if (r) r.closed = true;
    if (typeof cb === 'function') cb(er);
    if (er) nextTick(emitErrorCloseNT, this, er);
    else nextTick(emitCloseNT, this);
  };
  try {
    this._destroy(err || null, onDestroy);
  } catch (er) {
    onDestroy(er);
  }
  return this;
}

function setErrored(err, w, r) {
  if (!err) return;
  if (w && !w.errored) w.errored = err;
  if (r && !r.errored) r.errored = err;
}

function emitErrorCloseNT(stream, err) {
  emitErrorNT(stream, err);
  emitCloseNT(stream);
}

function emitCloseNT(stream) {
  const r = stream._readableState;
  const w = stream._writableState;
  if (w) w.closeEmitted = true;
  if (r) r.closeEmitted = true;
  if ((w && w.emitClose) || (r && r.emitClose)) stream.emit('close');
}

function emitErrorNT(stream, err) {
  const r = stream._readableState;
  const w = stream._writableState;
  if ((w && w.errorEmitted) || (r && r.errorEmitted)) return;
  if (w) w.errorEmitted = true;
  if (r) r.errorEmitted = true;
  stream.emit('error', err);
}

// Destroy the stream with err if it auto-destroys, otherwise just emit 'error'
function errorOrDestroy(stream, err, sync) {
  const r = stream._readableState;
  const w = stream._writableState;
  if ((w && w.destroyed) || (r && r.destroyed)) return;
  if ((r && r.autoDestroy) || (w && w.autoDestroy)) {
    stream.destroy(err);
  } else if (err) {
    setErrored(err, w, r);
    if (sync) nextTick(emitErrorNT, stream, err);
    else emitErrorNT(stream, err);
  }
}

function defaultDestroyImpl(err, cb) {
  cb(err);
}

// ---------------------------------------------------------------------------
// Stream (legacy base class)

function Stream(options) {
  EventEmitter.call(this, options);
}
Object.setPrototypeOf(Stream.prototype, EventEmitter.prototype);
Object.setPrototypeOf(Stream, EventEmitter);

// ---------------------------------------------------------------------------
// Readable

function ReadableState(options, stream, isDuplex) {
  if (typeof isDuplex !== 'boolean') isDuplex = stream instanceof Duplex;
  this.objectMode = !!(options && (options.objectMode || (isDuplex && options.readableObjectMode)));
  this.highWaterMark = getHighWaterMark(options, 'readableHighWaterMark', isDuplex, this.objectMode);

  this.buffer = [];
  this.length = 0;
  this.pipes = [];
  this.flowing = null;       // null: no consumer yet, true: flowing, false: paused
  this.ended = false;        // push(null) was called
  this.endEmitted = false;
  this.reading = false;      // _read() is in progress
  this.sync = true;          // push() happens inside _read(): defer 'readable'
  this.needReadable = false;
  this.emittedReadable = false;
  this.readableListening = false;
  this.resumeScheduled = false;
  this.paused = null;
  this.errorEmitted = false;
  this.emitClose = !options || options.emitClose !== false;
  this.autoDestroy = !options || options.autoDestroy !== false;
  this.destroyed = false;
  this.errored = null;
  this.closed = false;
  this.closeEmitted = false;
  this.defaultEncoding = (options && options.defaultEncoding) || 'utf8';
  this.awaitDrainWriters = new Set();
  this.readingMore = false;
  this.dataEmitted = false;
  this.decoder = null;
  this.encoding = null;
  if (options && options.encoding) {
    this.decoder = new StringDecoder(options.encoding);
    this.encoding = this.decoder.encoding;
  }
}

function Readable(options) {
  if (!(this instanceof Readable)) return new Readable(options);
  this._readableState = new ReadableState(options, this, this instanceof Duplex);
  if (options) {
    if (typeof options.read === 'function') this._read = options.read;
    if (typeof options.destroy === 'function') this._destroy = options.destroy;
  }
  Stream.call(this, options);
}
Object.setPrototypeOf(Readable.prototype, Stream.prototype);
Object.setPrototypeOf(Readable, Stream);

Readable.ReadableState = ReadableState;
Readable.prototype.destroy = destroy;
Readable.prototype._destroy = defaultDestroyImpl;

Readable.prototype.push = function push(chunk, encoding) {
  return readableAddChunk(this, chunk, encoding, false);
};

Readable.prototype.unshift = function unshift(chunk, encoding) {
  return readableAddChunk(this, chunk, encoding, true);
};

function readableAddChunk(stream, chunk, encoding, addToFront) {
  const state = stream._readableState;

  if (chunk === null) {
    state.reading = false;
    onEofChunk(stream, state);
    return false;
  }

  if (!state.objectMode) {
    if (!isValidChunk(chunk)) {
      errorOrDestroy(stream, errors.invalidChunk(chunk));
      return false;
    }
    // A latin1 string holds raw bytes: decode them like a Buffer
    if (typeof chunk === 'string' && encoding && state.decoder && normalizeEncoding(encoding) === 'latin1') {
      chunk = binding.byteStringToBuffer(chunk);
    }
  }

  if (state.objectMode || chunk.length > 0) {
    if (addToFront) {
      if (state.endEmitted) {
        errorOrDestroy(stream, errors.unshiftAfterEnd());
      } else if (state.destroyed || state.errored) {
        return false;
      } else {
        addChunk(stream, state, chunk, true);
      }
    } else if (state.ended) {
      errorOrDestroy(stream, errors.pushAfterEOF());
    } else if (state.destroyed || state.errored) {
      return false;
    } else {
      state.reading = false;
      if (state.decoder && !state.objectMode) {
        chunk = state.decoder.write(chunk);
        if (chunk.length === 0) maybeReadMore(stream, state);
        else addChunk(stream, state, chunk, false);
      } else {
        addChunk(stream, state, chunk, false);
      }
    }
  } else if (!addToFront) {
    state.reading = false;
    maybeReadMore(stream, state);
  }

  // false tells the producer to stop until _read() is called again
  return !state.ended && (state.length < state.highWaterMark || state.length === 0);
}

function addChunk(stream, state, chunk, addToFront) {
  if (state.flowing && state.length === 0 && !state.sync && stream.listenerCount('data') > 0) {
    // Nothing buffered and someone is listening: hand the chunk straight over
    state.awaitDrainWriters.clear();
    state.dataEmitted = true;
    stream.emit('data', chunk);
  } else {
    state.length += state.objectMode ? 1 : chunk.length;
    if (addToFront) state.buffer.unshift(chunk);
    else state.buffer.push(chunk);
    if (state.needReadable) emitReadable(stream);
  }
  maybeReadMore(stream, state);
}

Readable.prototype.isPaused = function isPaused() {
  const state = this._readableState;
  return state.paused === true || state.flowing === false;
};

Readable.prototype.setEncoding = function setEncoding(encoding) {
  const state = this._readableState;
  const decoder = new StringDecoder(encoding);
  state.decoder = decoder;
  state.encoding = decoder.encoding;
  // Re-decode what's already buffered
  let content = '';
  for (const chunk of state.buffer) content += decoder.write(chunk);
  state.buffer = content ? [content] : [];
  state.length = content.length;
  return this;
};

function howMuchToRead(n, state) {
  if (n <= 0 || (state.length === 0 && state.ended)) return 0;
  if (state.objectMode) return 1;
  if (Number.isNaN(n)) {
    // Whole buffer, or one chunk at a time while flowing
    if (state.flowing && state.length) return state.objectMode ? 1 : state.buffer[0].length;
    return state.length;
  }
  if (n <= state.length) return n;
  return state.ended ? state.length : 0;
}

Readable.prototype.read = function read(n) {
  n = n === undefined ? NaN : Number.parseInt(n, 10);
  const state = this._readableState;
  const nOrig = n;

  if (n > state.highWaterMark) {
    // Grow the buffer target to fit the request (next power of two)
    let hwm = n;
    if (hwm < 0x40000000) {
      hwm--;
      hwm |= hwm >>> 1; hwm |= hwm >>> 2; hwm |= hwm >>> 4; hwm |= hwm >>> 8; hwm |= hwm >>> 16;
      hwm++;
    }
    state.highWaterMark = hwm;
  }
  if (n !== 0) state.emittedReadable = false;

  // read(0) only triggers 'readable' or a refill
  if (n === 0 && state.needReadable &&
      ((state.highWaterMark !== 0 ? state.length >= state.highWaterMark : state.length > 0) || state.ended)) {
    if (state.length === 0 && state.ended) endReadable(this);
    else emitReadable(this);
    return null;
  }

  n = howMuchToRead(n, state);
  if (n === 0 && state.ended) {
    if (state.length === 0) endReadable(this);
    return null;
  }

  // Refill if below the high water mark after this read
  let doRead = state.needReadable;
  if (state.length === 0 || state.length - n < state.highWaterMark) doRead = true;
  if (state.ended || state.reading || state.destroyed || state.errored) {
    doRead = false;
  } else if (doRead) {
    state.reading = true;
    state.sync = true;
    if (state.length === 0) state.needReadable = true;
    try {
      this._read(state.highWaterMark);
    } catch (err) {
      errorOrDestroy(this, err);
    }
    state.sync = false;
    // A synchronous push may have made more data available
    if (!state.reading) n = howMuchToRead(nOrig, state);
  }

  let ret = n > 0 ? fromList(n, state) : null;
  if (ret === null) {
    state.needReadable = state.length <= state.highWaterMark;
    n = 0;
  } else {
    state.length -= n;
    state.awaitDrainWriters.clear();
  }

  if (state.length === 0) {
    if (!state.ended) state.needReadable = true;
    if (nOrig !== n && state.ended) endReadable(this);
  }

  if (ret !== null && !state.errorEmitted && !state.closeEmitted) {
    state.dataEmitted = true;
    this.emit('data', ret);
  }
  return ret;
};

// Take n units (bytes, characters or objects) off the front of the buffer
function fromList(n, state) {
  if (state.length === 0) return null;
  const buffer = state.buffer;
  if (state.objectMode) return buffer.shift();
  if (n >= state.length) {
    const all = buffer.length === 1 ? buffer[0] : joinChunks(buffer);
    state.buffer = [];
    return all;
  }

  const first = buffer[0];
  if (n < first.length) {
    buffer[0] = first.slice(n);
    return first.slice(0, n);
  }
  if (n === first.length) return buffer.shift();

  const parts = [];
  let remaining = n;
  while (remaining > 0) {
    const chunk = buffer[0];
    if (chunk.length <= remaining) {
      parts.push(buffer.shift());
      remaining -= chunk.length;
    } else {
      parts.push(chunk.slice(0, remaining));
      buffer[0] = chunk.slice(remaining);
      remaining = 0;
    }
  }
  return joinChunks(parts);
}

Readable.prototype._read = function _read() {
  throw errors.methodNotImplemented('_read()');
};

function onEofChunk(stream, state) {
  if (state.ended) return;
  if (state.decoder) {
    const rest = state.decoder.end();
    if (rest.length) {
      state.buffer.push(rest);
      state.length += state.objectMode ? 1 : rest.length;
    }
  }
  state.ended = true;

  if (state.sync) {
    emitReadable(stream);
  } else {
    state.needReadable = false;
    state.emittedReadable = true;
    emitReadable_(stream);
  }
}

function emitReadable(stream) {
  const state = stream._readableState;
  state.needReadable = false;
  if (!state.emittedReadable) {
    state.emittedReadable = true;
    nextTick(emitReadable_, stream);
  }
}

function emitReadable_(stream) {
  const state = stream._readableState;
  if (!state.destroyed && !state.errored && (state.length || state.ended)) {
    stream.emit('readable');
    state.emittedReadable = false;
  }
  state.needReadable = !state.flowing && !state.ended && state.length <= state.highWaterMark;
  flow(stream);
}

// Keep calling _read() while below the high water mark
function maybeReadMore(stream, state) {
  if (!state.readingMore) {
    state.readingMore = true;
    nextTick(maybeReadMore_, stream, state);
  }
}

function maybeReadMore_(stream, state) {
  while (!state.reading && !state.ended &&
         (state.length < state.highWaterMark || (state.flowing && state.length === 0))) {
    const len = state.length;
    stream.read(0);
    if (len === state.length) break;  // _read() didn't push synchronously
  }
  state.readingMore = false;
}

Readable.prototype.pipe = function pipe(dest, pipeOpts) {
  const src = this;
  const state = this._readableState;
  state.pipes.push(dest);

  const doEnd = !pipeOpts || pipeOpts.end !== false;
  const endFn = doEnd ? onend : unpipe;
  if (state.endEmitted) nextTick(endFn);
  else src.once('end', endFn);

  dest.on('unpipe', onunpipe);
  function onunpipe(readable, unpipeInfo) {
    if (readable === src && unpipeInfo && unpipeInfo.hasUnpiped === false) {
      unpipeInfo.hasUnpiped = true;
      cleanup();
    }
  }

  function onend() {
    dest.end();
  }

  let ondrain;
  let cleanedUp = false;
  function cleanup() {
    dest.removeListener('close', onclose);
    dest.removeListener('finish', onfinish);
    if (ondrain) dest.removeListener('drain', ondrain);
    dest.removeListener('error', onerror);
    dest.removeListener('unpipe', onunpipe);
    src.removeListener('end', onend);
    src.removeListener('end', unpipe);
    src.removeListener('data', ondata);
    cleanedUp = true;
    // If we were waiting on this destination, stop waiting
    if (ondrain && state.awaitDrainWriters.size > 0 && (!dest._writableState || dest._writableState.needDrain)) ondrain();
  }

  function pause() {
    if (!cleanedUp) {
      state.awaitDrainWriters.add(dest);
      src.pause();
    }
    if (!ondrain) {
      ondrain = pipeOnDrain(src, dest);
      dest.on('drain', ondrain);
    }
  }

  // Backpressure: stop reading when the destination's buffer is full
  src.on('data', ondata);
  function ondata(chunk) {
    if (dest.write(chunk) === false) pause();
  }

  function onerror(er) {
    unpipe();
    dest.removeListener('error', onerror);
    if (dest.listenerCount('error') === 0) {
      const s = dest._writableState || dest._readableState;
      if (s && !s.errorEmitted) errorOrDestroy(dest, er);
      else dest.emit('error', er);
    }
  }
  if (typeof dest.prependListener === 'function') dest.prependListener('error', onerror);
  else dest.on('error', onerror);

  function onclose() {
    dest.removeListener('finish', onfinish);
    unpipe();
  }
  dest.once('close', onclose);

  function onfinish() {
    dest.removeListener('close', onclose);
    unpipe();
  }
  dest.once('finish', onfinish);

  function unpipe() {
    src.unpipe(dest);
  }

  dest.emit('pipe', src);

  if (dest.writableNeedDrain === true) pause();
  else if (!state.flowing) src.resume();

  return dest;
};

function pipeOnDrain(src, dest) {
  return function pipeOnDrainFunctionResult() {
    const state = src._readableState;
    state.awaitDrainWriters.delete(dest);
    if (state.awaitDrainWriters.size === 0 && src.listenerCount('data') > 0) src.resume();
  };
}

Readable.prototype.unpipe = function unpipe(dest) {
  const state = this._readableState;
  if (state.pipes.length === 0) return this;

  if (!dest) {
    const dests = state.pipes;
    state.pipes = [];
    this.pause();
    for (const d of dests) d.emit('unpipe', this, { hasUnpiped: false });
    return this;
  }

  const index = state.pipes.indexOf(dest);
  if (index === -1) return this;
  state.pipes.splice(index, 1);
  if (state.pipes.length === 0) this.pause();
  dest.emit('unpipe', this, { hasUnpiped: false });
  return this;
};

// Adding a 'data' listener switches to flowing mode; 'readable' to paused mode
Readable.prototype.on = function on(ev, fn) {
  const res = Stream.prototype.on.call(this, ev, fn);
  const state = this._readableState;

  if (ev === 'data') {
    state.readableListening = this.listenerCount('readable') > 0;
    if (state.flowing !== false) this.resume();
  } else if (ev === 'readable') {
    if (!state.endEmitted && !state.readableListening) {
      state.readableListening = state.needReadable = true;
      state.flowing = false;
      state.emittedReadable = false;
      if (state.length) emitReadable(this);
      else if (!state.reading) nextTick(nReadingNextTick, this);
    }
  }
  return res;
};
Readable.prototype.addListener = Readable.prototype.on;

Readable.prototype.removeListener = function removeListener(ev, fn) {
  const res = Stream.prototype.removeListener.call(this, ev, fn);
  if (ev === 'readable') nextTick(updateReadableListening, this);
  return res;
};
Readable.prototype.off = Readable.prototype.removeListener;

Readable.prototype.removeAllListeners = function removeAllListeners(ev) {
  const res = Stream.prototype.removeAllListeners.apply(this, arguments);
  if (ev === 'readable' || ev === undefined) nextTick(updateReadableListening, this);
  return res;
};

function updateReadableListening(self) {
  const state = self._readableState;
  state.readableListening = self.listenerCount('readable') > 0;
  if (state.resumeScheduled && state.paused === false) {
    state.flowing = true;
  } else if (self.listenerCount('data') > 0) {
    self.resume();
  } else if (!state.readableListening) {
    state.flowing = null;
  }
}

function nReadingNextTick(self) {
  self.read(0);
}

Readable.prototype.resume = function resume() {
  const state = this._readableState;
  if (!state.flowing) {
    // With a 'readable' listener, data is pulled with read() instead
    state.flowing = !state.readableListening;
    if (!state.resumeScheduled) {
      state.resumeScheduled = true;
      nextTick(resume_, this, state);
    }
  }
  state.paused = false;
  return this;
};

function resume_(stream, state) {
  if (!state.reading) stream.read(0);
  state.resumeScheduled = false;
  stream.emit('resume');
  flow(stream);
  if (state.flowing && !state.reading) stream.read(0);
}

Readable.prototype.pause = function pause() {
  const state = this._readableState;
  if (state.flowing !== false) {
    state.flowing = false;
    this.emit('pause');
  }
  state.paused = true;
  return this;
};

function flow(stream) {
  const state = stream._readableState;
  while (state.flowing && stream.read() !== null);
}

function endReadable(stream) {
  const state = stream._readableState;
  if (!state.endEmitted) {
    state.ended = true;
    nextTick(endReadableNT, state, stream);
  }
}

function endReadableNT(state, stream) {
  if (!state.errorEmitted && !state.closeEmitted && !state.endEmitted && state.length === 0) {
    state.endEmitted = true;
    stream.emit('end');

    const wState = stream._writableState;
    if (wState && stream.allowHalfOpen === false && stream.writable) {
      // Duplex without half-open: end the writable side too
      nextTick(endWritableNT, stream);
    } else if (state.autoDestroy) {
      // Wait for the writable side of a Duplex before destroying
      if (!wState || (wState.autoDestroy && (wState.finished || wState.writable === false))) stream.destroy();
    }
  }
}

function endWritableNT(stream) {
  if (stream.writable && !stream.writableEnded && !stream.destroyed) stream.end();
}

// for await (const chunk of readable)
Readable.prototype[Symbol.asyncIterator] = function asyncIterator() {
  return createAsyncIterator(this);
};

async function* createAsyncIterator(stream) {
  let callback = nop;
  function next(resolve) {
    if (this === stream) {
      callback();
      callback = nop;
    } else {
      callback = resolve;
    }
  }

  stream.on('readable', next);

  let error;  // undefined: running, null: ended cleanly, else the error
  const cleanup = finished(stream, { writable: false }, (err) => {
    error = err || null;
    callback();
    callback = nop;
  });

  try {
    while (true) {
      const chunk = stream.destroyed ? null : stream.read();
      if (chunk !== null) yield chunk;
      else if (error) throw error;
      else if (error === null) return;
      else await new Promise(next);
    }
  } catch (err) {
    error = err;
    throw error;
  } finally {
    const state = stream._readableState;
    if ((error || state.autoDestroy !== false) && (error === undefined || state.autoDestroy)) {
      // Loop exited early (break/return/throw): the rest of the stream isn't wanted
      stream.destroy(null);
    } else {
      stream.off('readable', next);
      cleanup();
    }
  }
}

// Readable.from(iterable[, options]): stream the values of an (async) iterable
Readable.from = function from(iterable, opts) {
  if (typeof iterable === 'string' || Buffer.isBuffer(iterable)) {
    return new Readable(Object.assign({ objectMode: true }, opts, {
      read() {
        this.push(iterable);
        this.push(null);
      },
    }));
  }

  let isAsync;
  let iterator;
  if (iterable && typeof iterable[Symbol.asyncIterator] === 'function') {
    isAsync = true;
    iterator = iterable[Symbol.asyncIterator]();
  } else if (iterable && typeof iterable[Symbol.iterator] === 'function') {
    isAsync = false;
    iterator = iterable[Symbol.iterator]();
  } else {
    throw errors.invalidArg('iterable', 'an Iterable');
  }

  const readable = new Readable(Object.assign({ objectMode: true, highWaterMark: 1 }, opts));
  let reading = false;

  readable._read = function () {
    if (!reading) {
      reading = true;
      next();
    }
  };

  readable._destroy = function (error, cb) {
    close(error).then(() => nextTick(cb, error), (e) => nextTick(cb, e || error));
  };

  async function close(error) {
    const hadError = error !== undefined && error !== null;
    if (hadError && typeof iterator.throw === 'function') {
      const { value, done } = await iterator.throw(error);
      await value;
      if (done) return;
    }
    if (typeof iterator.return === 'function') {
      const { value } = await iterator.return();
      await value;
    }
  }

  async function next() {
    for (;;) {
      try {
        const { value, done } = isAsync ? await iterator.next() : iterator.next();
        if (done) {
          readable.push(null);
        } else {
          const res = value && typeof value.then === 'function' ? await value : value;
          if (res === null) {
            reading = false;
            throw errors.nullValues();
          } else if (readable.push(res)) {
            continue;
          } else {
            reading = false;
          }
        }
      } catch (err) {
        readable.destroy(err);
      }
      break;
    }
  }

  return readable;
};

function defineGetters(proto, getters) {
  for (const name of Object.keys(getters)) {
    const desc = typeof getters[name] === 'function'
      ? { get: getters[name], configurable: true, enumerable: false }
      : Object.assign({ configurable: true, enumerable: false }, getters[name]);
    Object.defineProperty(proto, name, desc);
  }
}

defineGetters(Readable.prototype, {
  readable: {
    get() {
      const r = this._readableState;
      return !!r && r.readable !== false && !r.destroyed && !r.errorEmitted && !r.endEmitted;
    },
    set(val) {
      if (this._readableState) this._readableState.readable = !!val;
    },
  },
  readableDidRead() { return this._readableState.dataEmitted; },
  readableHighWaterMark() { return this._readableState.highWaterMark; },
  readableBuffer() { return this._readableState && this._readableState.buffer; },
  readableFlowing: {
    get() { return this._readableState.flowing; },
    set(state) { if (this._readableState) this._readableState.flowing = state; },
  },
  readableLength() { return this._readableState.length; },
  readableObjectMode() { return this._readableState ? this._readableState.objectMode : false; },
  readableEncoding() { return this._readableState ? this._readableState.encoding : null; },
  readableEnded() { return this._readableState ? this._readableState.endEmitted : false; },
  readableAborted() {
    const r = this._readableState;
    return !!(r && r.readable !== false && (r.destroyed || r.errored) && !r.endEmitted);
  },
  errored() { return this._readableState ? this._readableState.errored : null; },
  closed() { return this._readableState ? this._readableState.closed : false; },
  destroyed: {
    get() { return this._readableState ? this._readableState.destroyed : false; },
    set(value) { if (this._readableState) this._readableState.destroyed = value; },
  },
});

// ---------------------------------------------------------------------------
// Writable

function WritableState(options, stream, isDuplex) {
  if (typeof isDuplex !== 'boolean') isDuplex = stream instanceof Duplex;
  this.objectMode = !!(options && (options.objectMode || (isDuplex && options.writableObjectMode)));
  this.highWaterMark = getHighWaterMark(options, 'writableHighWaterMark', isDuplex, this.objectMode);

  this.finalCalled = false;
  this.needDrain = false;     // write() returned false; emit 'drain' when emptied
  this.ending = false;        // end() was called
  this.ended = false;
  this.finished = false;      // 'finish' was emitted
  this.destroyed = false;
  this.defaultEncoding = (options && options.defaultEncoding) || 'utf8';
  this.length = 0;            // queued + in-flight units, for backpressure
  this.writing = false;
  this.corked = 0;
  this.sync = true;           // _write() callback is being called synchronously
  this.bufferProcessing = false;
  this.onwrite = onwrite.bind(undefined, stream);
  this.writecb = null;
  this.writelen = 0;
  this.buffered = [];         // writes waiting for the current one: { chunk, encoding, callback }
  this.allNoop = true;
  this.pendingcb = 0;         // outstanding callbacks, including 'finish'
  this.prefinished = false;
  this.errorEmitted = false;
  this.emitClose = !options || options.emitClose !== false;
  this.autoDestroy = !options || options.autoDestroy !== false;
  this.errored = null;
  this.closed = false;
  this.closeEmitted = false;
  this.onFinished = [];       // end() callbacks
}

function Writable(options) {
  if (!(this instanceof Writable) && !(this instanceof Duplex)) return new Writable(options);
  this._writableState = new WritableState(options, this, this instanceof Duplex);
  if (options) {
    if (typeof options.write === 'function') this._write = options.write;
    if (typeof options.writev === 'function') this._writev = options.writev;
    if (typeof options.destroy === 'function') this._destroy = options.destroy;
    if (typeof options.final === 'function') this._final = options.final;
  }
  Stream.call(this, options);
}
Object.setPrototypeOf(Writable.prototype, Stream.prototype);
Object.setPrototypeOf(Writable, Stream);

// A Duplex is a Writable too, though it inherits from Readable
Object.defineProperty(Writable, Symbol.hasInstance, {
  value: function (object) {
    if (Function.prototype[Symbol.hasInstance].call(this, object)) return true;
    if (this !== Writable) return false;
    return !!(object && object._writableState instanceof WritableState);
  },
});

Writable.WritableState = WritableState;
Writable.prototype.destroy = destroy;
Writable.prototype._destroy = defaultDestroyImpl;

// Returns true/false like write(), or an Error if the write was rejected
function writeImpl(stream, chunk, encoding, cb) {
  const state = stream._writableState;

  if (typeof encoding === 'function') {
    cb = encoding;
    encoding = state.defaultEncoding;
  } else {
    if (!encoding) encoding = state.defaultEncoding;
    if (typeof cb !== 'function') cb = nop;
  }

  if (chunk === null) throw errors.nullValues();
  if (!state.objectMode) {
    if (Buffer.isBuffer(chunk)) encoding = 'buffer';
    else if (typeof chunk !== 'string') throw errors.invalidChunk(chunk);
  }

  let err;
  if (state.ending) err = errors.writeAfterEnd();
  else if (state.destroyed) err = errors.destroyed('write');
  if (err) {
    nextTick(cb, err);
    errorOrDestroy(stream, err, true);
    return err;
  }

  state.pendingcb++;
  return writeOrBuffer(stream, state, chunk, encoding, cb);
}

function writeOrBuffer(stream, state, chunk, encoding, callback) {
  const len = state.objectMode ? 1 : chunk.length;
  state.length += len;

  if (state.writing || state.corked || state.errored) {
    state.buffered.push({ chunk, encoding, callback });
    if (state.allNoop && callback !== nop) state.allNoop = false;
  } else {
    doWrite(stream, state, false, len, chunk, encoding, callback);
  }

  // Checked after the write, so a _write() that completes synchronously never
  // signals backpressure (matches Node.js 22+)
  const ret = state.length < state.highWaterMark || state.length === 0;
  if (!ret) state.needDrain = true;

  // false tells the producer to wait for 'drain'
  return ret && !state.errored && !state.destroyed;
}

function doWrite(stream, state, writev, len, chunk, encoding, cb) {
  state.writelen = len;
  state.writecb = cb;
  state.writing = true;
  state.sync = true;
  if (state.destroyed) state.onwrite(errors.destroyed('write'));
  else if (writev) stream._writev(chunk, state.onwrite);
  else stream._write(chunk, encoding, state.onwrite);
  state.sync = false;
}

function onwrite(stream, er) {
  const state = stream._writableState;
  const sync = state.sync;
  const cb = state.writecb;

  if (typeof cb !== 'function') {
    errorOrDestroy(stream, errors.multipleCallback());
    return;
  }

  state.writing = false;
  state.writecb = null;
  state.length -= state.writelen;
  state.writelen = 0;

  if (er) {
    if (!state.errored) state.errored = er;
    if (sync) nextTick(onwriteError, stream, state, er, cb);
    else onwriteError(stream, state, er, cb);
  } else {
    if (state.buffered.length > 0) clearBuffer(stream, state);
    // Callbacks never run synchronously inside write()
    if (sync) nextTick(afterWrite, stream, state, cb);
    else afterWrite(stream, state, cb);
  }
}

function onwriteError(stream, state, er, cb) {
  --state.pendingcb;
  cb(er);
  errorBuffer(state);
  errorOrDestroy(stream, er);
}

function afterWrite(stream, state, cb) {
  const needDrain = !state.ending && !stream.destroyed && state.length === 0 && state.needDrain;
  if (needDrain) {
    state.needDrain = false;
    stream.emit('drain');
  }
  state.pendingcb--;
  cb();
  if (state.destroyed) errorBuffer(state);
  finishMaybe(stream, state);
}

// Fail every queued write (the stream was destroyed or errored)
function errorBuffer(state) {
  if (state.writing) return;
  for (const { chunk, callback } of state.buffered.splice(0)) {
    state.length -= state.objectMode ? 1 : chunk.length;
    state.pendingcb--;
    callback(state.errored || errors.destroyed('write'));
  }
  for (const cb of state.onFinished.splice(0)) cb(state.errored || errors.destroyed('end'));
}

function clearBuffer(stream, state) {
  if (state.corked || state.bufferProcessing || state.destroyed) return;
  const buffered = state.buffered;
  if (buffered.length === 0) return;

  state.bufferProcessing = true;
  if (buffered.length > 1 && stream._writev) {
    // One _writev() call for the whole queue
    state.pendingcb -= buffered.length - 1;
    const chunks = buffered.splice(0);
    const callback = state.allNoop ? nop : (err) => {
      for (const entry of chunks) entry.callback(err);
    };
    doWrite(stream, state, true, state.length, chunks, '', callback);
  } else {
    do {
      const { chunk, encoding, callback } = buffered.shift();
      const len = state.objectMode ? 1 : chunk.length;
      doWrite(stream, state, false, len, chunk, encoding, callback);
    } while (buffered.length > 0 && !state.writing);
  }
  if (buffered.length === 0) state.allNoop = true;
  state.bufferProcessing = false;
}

Writable.prototype.write = function write(chunk, encoding, cb) {
  return writeImpl(this, chunk, encoding, cb) === true;
};

Writable.prototype.cork = function cork() {
  this._writableState.corked++;
};

Writable.prototype.uncork = function uncork() {
  const state = this._writableState;
  if (state.corked) {
    state.corked--;
    if (!state.writing) clearBuffer(this, state);
  }
};

Writable.prototype.setDefaultEncoding = function setDefaultEncoding(encoding) {
  this._writableState.defaultEncoding = normalizeEncoding(encoding);
  return this;
};

Writable.prototype._write = function _write(chunk, encoding, cb) {
  if (this._writev) this._writev([{ chunk, encoding }], cb);
  else throw errors.methodNotImplemented('_write()');
};

Writable.prototype._writev = null;

Writable.prototype.end = function end(chunk, encoding, cb) {
  const state = this._writableState;

  if (typeof chunk === 'function') {
    cb = chunk;
    chunk = null;
    encoding = null;
  } else if (typeof encoding === 'function') {
    cb = encoding;
    encoding = null;
  }

  let err;
  if (chunk !== null && chunk !== undefined) {
    const ret = writeImpl(this, chunk, encoding);
    if (ret instanceof Error) err = ret;
  }

  // end() flushes corked writes
  if (state.corked) {
    state.corked = 1;
    this.uncork();
  }

  if (err) {
    // Nothing to do
  } else if (!state.errored && !state.ending) {
    state.ending = true;
    finishMaybe(this, state, true);
    state.ended = true;
  } else if (state.finished) {
    err = errors.alreadyFinished('end');
  } else if (state.destroyed) {
    err = errors.destroyed('end');
  }

  if (typeof cb === 'function') {
    if (err || state.finished) nextTick(cb, err);
    else state.onFinished.push(cb);
  }
  return this;
};

function needFinish(state) {
  return state.ending && !state.destroyed && state.length === 0 && !state.errored &&
    state.buffered.length === 0 && !state.finished && !state.writing &&
    !state.errorEmitted && !state.closeEmitted;
}

function callFinal(stream, state) {
  let called = false;
  function onFinish(err) {
    if (called) {
      errorOrDestroy(stream, errors.multipleCallback());
      return;
    }
    called = true;
    state.pendingcb--;
    if (err) {
      for (const cb of state.onFinished.splice(0)) cb(err);
      errorOrDestroy(stream, err, state.sync);
    } else if (needFinish(state)) {
      state.prefinished = true;
      stream.emit('prefinish');
      state.pendingcb++;
      nextTick(finish, stream, state);
    }
  }

  state.sync = true;
  state.pendingcb++;
  try {
    stream._final(onFinish);
  } catch (err) {
    onFinish(err);
  }
  state.sync = false;
}

function prefinish(stream, state) {
  if (!state.prefinished && !state.finalCalled) {
    if (typeof stream._final === 'function' && !state.destroyed) {
      state.finalCalled = true;
      callFinal(stream, state);
    } else {
      state.prefinished = true;
      stream.emit('prefinish');
    }
  }
}

function finishMaybe(stream, state, sync) {
  if (!needFinish(state)) return;
  prefinish(stream, state);
  if (state.pendingcb !== 0) return;
  if (sync) {
    state.pendingcb++;
    nextTick(() => {
      if (needFinish(state)) finish(stream, state);
      else state.pendingcb--;
    });
  } else if (needFinish(state)) {
    state.pendingcb++;
    finish(stream, state);
  }
}

function finish(stream, state) {
  state.pendingcb--;
  state.finished = true;

  for (const cb of state.onFinished.splice(0)) cb();
  stream.emit('finish');

  if (state.autoDestroy) {
    // Wait for the readable side of a Duplex before destroying
    const rState = stream._readableState;
    if (!rState || (rState.autoDestroy && (rState.endEmitted || rState.readable === false))) stream.destroy();
  }
}

const writableGetters = {
  closed() { return this._writableState ? this._writableState.closed : false; },
  destroyed: {
    get() { return this._writableState ? this._writableState.destroyed : false; },
    set(value) { if (this._writableState) this._writableState.destroyed = value; },
  },
  writable: {
    get() {
      const w = this._writableState;
      return !!w && w.writable !== false && !w.destroyed && !w.errored && !w.ending && !w.ended;
    },
    set(val) {
      if (this._writableState) this._writableState.writable = !!val;
    },
  },
  writableFinished() { return this._writableState ? this._writableState.finished : false; },
  writableObjectMode() { return this._writableState ? this._writableState.objectMode : false; },
  writableEnded() { return this._writableState ? this._writableState.ending : false; },
  writableNeedDrain() {
    const w = this._writableState;
    return w ? !w.destroyed && !w.ending && w.needDrain : false;
  },
  writableHighWaterMark() { return this._writableState && this._writableState.highWaterMark; },
  writableCorked() { return this._writableState ? this._writableState.corked : 0; },
  writableLength() { return this._writableState && this._writableState.length; },
  errored() { return this._writableState ? this._writableState.errored : null; },
  writableAborted() {
    const w = this._writableState;
    return !!(w && w.writable !== false && (w.destroyed || w.errored) && !w.finished);
  },
};
defineGetters(Writable.prototype, writableGetters);

// ---------------------------------------------------------------------------
// Duplex: Readable + Writable

function Duplex(options) {
  if (!(this instanceof Duplex)) return new Duplex(options);
  Readable.call(this, options);
  Writable.call(this, options);

  if (options) {
    this.allowHalfOpen = options.allowHalfOpen !== false;
    if (options.readable === false) {
      this._readableState.readable = false;
      this._readableState.ended = true;
      this._readableState.endEmitted = true;
    }
    if (options.writable === false) {
      this._writableState.writable = false;
      this._writableState.ending = true;
      this._writableState.ended = true;
      this._writableState.finished = true;
    }
  } else {
    this.allowHalfOpen = true;
  }
}
Object.setPrototypeOf(Duplex.prototype, Readable.prototype);
Object.setPrototypeOf(Duplex, Readable);

defineGetters(Duplex.prototype, {
  destroyed: {
    get() {
      if (this._readableState === undefined || this._writableState === undefined) return false;
      return this._readableState.destroyed && this._writableState.destroyed;
    },
    set(value) {
      if (this._readableState && this._writableState) {
        this._readableState.destroyed = value;
        this._writableState.destroyed = value;
      }
    },
  },
  errored() {
    return (this._writableState && this._writableState.errored) ||
      (this._readableState && this._readableState.errored) || null;
  },
  closed() {
    return !!((this._readableState && this._readableState.closed) ||
      (this._writableState && this._writableState.closed));
  },
});

// Writable methods and getters that Readable doesn't already provide
for (const key of Object.getOwnPropertyNames(Writable.prototype)) {
  if (key === 'constructor' || key in Duplex.prototype) continue;
  Object.defineProperty(Duplex.prototype, key, Object.getOwnPropertyDescriptor(Writable.prototype, key));
}

// ---------------------------------------------------------------------------
// Transform: a Duplex whose output is computed from its input

function Transform(options) {
  if (!(this instanceof Transform)) return new Transform(options);

  Duplex.call(this, options);

  // push() from inside _transform() shouldn't defer 'readable'
  this._readableState.sync = false;
  this[kTransformCallback] = null;

  if (options) {
    if (typeof options.transform === 'function') this._transform = options.transform;
    if (typeof options.flush === 'function') this._flush = options.flush;
  }

  // If the user overrides _final, still flush when the writable side finishes
  this.on('prefinish', transformPrefinish);
}
Object.setPrototypeOf(Transform.prototype, Duplex.prototype);
Object.setPrototypeOf(Transform, Duplex);

function transformFinal(cb) {
  if (typeof this._flush === 'function' && !this.destroyed) {
    this._flush((er, data) => {
      if (er) {
        if (cb) cb(er);
        else this.destroy(er);
        return;
      }
      if (data !== null && data !== undefined) this.push(data);
      this.push(null);
      if (cb) cb();
    });
  } else {
    this.push(null);
    if (cb) cb();
  }
}

function transformPrefinish() {
  if (this._final !== transformFinal) transformFinal.call(this);
}

Transform.prototype._final = transformFinal;

Transform.prototype._transform = function _transform() {
  throw errors.methodNotImplemented('_transform()');
};

Transform.prototype._write = function _write(chunk, encoding, callback) {
  const rState = this._readableState;
  const wState = this._writableState;
  const length = rState.length;

  this._transform(chunk, encoding, (err, val) => {
    if (err) {
      callback(err);
      return;
    }
    if (val !== null && val !== undefined) this.push(val);

    if (wState.ended || length === rState.length || rState.length < rState.highWaterMark) {
      callback();
    } else {
      // Readable side is full: accept more input once it's read
      this[kTransformCallback] = callback;
    }
  });
};

Transform.prototype._read = function _read() {
  if (this[kTransformCallback]) {
    const callback = this[kTransformCallback];
    this[kTransformCallback] = null;
    callback();
  }
};

// PassThrough: a Transform that outputs its input unchanged
function PassThrough(options) {
  if (!(this instanceof PassThrough)) return new PassThrough(options);
  Transform.call(this, options);
}
Object.setPrototypeOf(PassThrough.prototype, Transform.prototype);
Object.setPrototypeOf(PassThrough, Transform);

PassThrough.prototype._transform = function _transform(chunk, encoding, cb) {
  cb(null, chunk);
};

// ---------------------------------------------------------------------------
// finished() and pipeline()

function isReadableFinished(stream, strict) {
  if (!isReadableNodeStream(stream)) return null;
  const r = stream._readableState;
  if (!r || r.errored) return false;
  return !!(r.endEmitted || (strict === false && r.ended === true && r.length === 0));
}

function isWritableFinished(stream, strict) {
  if (!isWritableNodeStream(stream)) return null;
  if (stream.writableFinished === true) return true;
  const w = stream._writableState;
  if (!w || w.errored) return false;
  return !!(w.finished || (strict === false && w.ended === true && w.length === 0));
}

function willEmitClose(stream) {
  const state = stream._writableState || stream._readableState;
  return !!(state && state.autoDestroy && state.emitClose && state.closed === false);
}

// finished(stream[, options], callback): call back once when the stream has
// ended, finished, errored or closed early. Returns a function that removes the listeners.
function finished(stream, options, callback) {
  if (typeof options === 'function') {
    callback = options;
    options = {};
  } else if (!options) {
    options = {};
  }
  if (typeof callback !== 'function') throw errors.invalidArg('callback', 'of type function');

  let called = false;
  const cb = (err) => {
    if (called) return;
    called = true;
    callback.call(stream, err);
  };

  const readable = options.readable !== undefined ? options.readable : isReadableNodeStream(stream);
  const writable = options.writable !== undefined ? options.writable : isWritableNodeStream(stream);
  const wState = stream._writableState;
  const rState = stream._readableState;

  let emitsClose = willEmitClose(stream) &&
    isReadableNodeStream(stream) === readable && isWritableNodeStream(stream) === writable;

  let writableFinished = isWritableFinished(stream, false);
  const onfinish = () => {
    writableFinished = true;
    if (stream.destroyed) emitsClose = false;
    if (emitsClose && (!stream.readable || readable)) return;
    if (!readable || readableFinished) cb();
  };

  let readableFinished = isReadableFinished(stream, false);
  const onend = () => {
    readableFinished = true;
    if (stream.destroyed) emitsClose = false;
    if (emitsClose && (!stream.writable || writable)) return;
    if (!writable || writableFinished) cb();
  };

  const onerror = (err) => cb(err);

  const onclose = () => {
    const errored = (wState && wState.errored) || (rState && rState.errored);
    if (errored && typeof errored !== 'boolean') return cb(errored);
    if (readable && !readableFinished && isReadableNodeStream(stream) && !isReadableFinished(stream, false)) {
      return cb(errors.prematureClose());
    }
    if (writable && !writableFinished && !isWritableFinished(stream, false)) {
      return cb(errors.prematureClose());
    }
    cb();
  };

  stream.on('end', onend);
  stream.on('finish', onfinish);
  if (options.error !== false) stream.on('error', onerror);
  stream.on('close', onclose);

  const closed = (wState && wState.closed) || (rState && rState.closed);
  if (closed) {
    nextTick(onclose);
  } else if ((wState && wState.errorEmitted) || (rState && rState.errorEmitted)) {
    if (!emitsClose) nextTick(onclose);
  } else if (!readable && (!emitsClose || stream.readable) && writableFinished) {
    nextTick(onclose);
  } else if (!writable && (!emitsClose || stream.writable) && readableFinished) {
    nextTick(onclose);
  }

  return function cleanup() {
    called = true;
    stream.removeListener('end', onend);
    stream.removeListener('finish', onfinish);
    stream.removeListener('error', onerror);
    stream.removeListener('close', onclose);
  };
}

// pipeline(source, ...transforms, destination, callback): pipe the streams
// together, destroy all of them if any fails, and call back once all are done.
function pipeline(...streams) {
  const callback = streams.length > 0 && typeof streams[streams.length - 1] === 'function' ? streams.pop() : null;
  if (!callback) throw errors.invalidArg('callback', 'of type function');
  if (streams.length === 1 && Array.isArray(streams[0])) streams = streams[0];
  streams = streams.slice();
  if (streams.length < 2) {
    throw makeError(TypeError, 'ERR_MISSING_ARGS', 'The "streams" argument must be specified');
  }

  // The source may be any iterable or async iterable
  if (!isNodeStream(streams[0])) streams[0] = Readable.from(streams[0]);
  for (let i = 1; i < streams.length; i++) {
    if (!isWritableNodeStream(streams[i])) {
      throw errors.invalidArg(`streams[${i}]`, 'a writable stream');
    }
  }

  let error;
  let destroyed = false;
  let pending = streams.length;

  function onStreamDone(err) {
    if (err && (!error || error.code === 'ERR_STREAM_PREMATURE_CLOSE')) error = err;
    if (error && !destroyed) {
      destroyed = true;
      for (const s of streams) {
        if (!s.destroyed && typeof s.destroy === 'function') s.destroy(error);
      }
    }
    if (--pending === 0) nextTick(callback, error);
  }

  streams.forEach((s, i) => {
    finished(s, { readable: i < streams.length - 1, writable: i > 0 }, onStreamDone);
  });
  for (let i = 1; i < streams.length; i++) streams[i - 1].pipe(streams[i]);
  return streams[streams.length - 1];
}

const promises = {
  pipeline(...streams) {
    return new Promise((resolve, reject) => {
      pipeline(...streams, (err) => err ? reject(err) : resolve());
    });
  },
  finished(stream, options) {
    return new Promise((resolve, reject) => {
      finished(stream, options, (err) => err ? reject(err) : resolve());
    });
  },
};

Object.assign(Stream, {
  Stream, Readable, Writable, Duplex, Transform, PassThrough,
  pipeline, finished, promises,
  isReadable: (s) => isReadableNodeStream(s) && !!s.readable,
  isErrored: (s) => !!(s && s.errored),
});

return Stream;
})
)JS";

// Set up stream module (requires SetupBuffer and SetupEvents first): exposed
// as global `stream` and returned by require('stream')
void SetupStream(Isolate* isolate, Local<Context> context) {
    Local<Object> global = context->Global();

    Local<Object> binding = Object::New(isolate);
    frogjs::SetFunction(isolate, context, binding, "toByteString", ToByteString);
    frogjs::SetFunction(isolate, context, binding, "byteStringToUtf8", ByteStringToUtf8);
    frogjs::SetFunction(isolate, context, binding, "byteStringToBuffer", ByteStringToBuffer);

    Local<Value> argv[3] = {
        global->Get(context, String::NewFromUtf8(isolate, "events").ToLocalChecked()).ToLocalChecked(),
        global->Get(context, String::NewFromUtf8(isolate, "Buffer").ToLocalChecked()).ToLocalChecked(),
        binding,
    };

    Local<Value> stream;
    if (!frogjs::RunJsModule(isolate, context, "stream", kStreamSource, 3, argv).ToLocal(&stream)) {
        return;
    }
    global->Set(context, String::NewFromUtf8(isolate, "stream").ToLocalChecked(), stream).Check();
}
