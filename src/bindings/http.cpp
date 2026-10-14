#include <v8.h>
#include "frogjs/v8_compat.h"
#include "frogjs/buffer.h"
#include <vector>

using namespace v8;

// The http module is written in JavaScript on top of the net module. Sockets
// run in 'latin1' mode, so all protocol data is handled as byte strings (one
// char per byte, codes 0-255); toBytes/bytesToUtf8 convert user data at the edges.
static const char* kHttpSource = R"JS(
(function (net, binding, setTimeout, clearTimeout) {
'use strict';

const METHODS = [
  'ACL', 'BIND', 'CHECKOUT', 'CONNECT', 'COPY', 'DELETE', 'GET', 'HEAD', 'LINK',
  'LOCK', 'M-SEARCH', 'MERGE', 'MKACTIVITY', 'MKCALENDAR', 'MKCOL', 'MOVE',
  'NOTIFY', 'OPTIONS', 'PATCH', 'POST', 'PROPFIND', 'PROPPATCH', 'PURGE', 'PUT',
  'QUERY', 'REBIND', 'REPORT', 'SEARCH', 'SOURCE', 'SUBSCRIBE', 'TRACE',
  'UNBIND', 'UNLINK', 'UNLOCK', 'UNSUBSCRIBE',
];

const STATUS_CODES = {
  100: 'Continue', 101: 'Switching Protocols', 102: 'Processing', 103: 'Early Hints',
  200: 'OK', 201: 'Created', 202: 'Accepted', 203: 'Non-Authoritative Information',
  204: 'No Content', 205: 'Reset Content', 206: 'Partial Content', 207: 'Multi-Status',
  208: 'Already Reported', 226: 'IM Used',
  300: 'Multiple Choices', 301: 'Moved Permanently', 302: 'Found', 303: 'See Other',
  304: 'Not Modified', 305: 'Use Proxy', 307: 'Temporary Redirect', 308: 'Permanent Redirect',
  400: 'Bad Request', 401: 'Unauthorized', 402: 'Payment Required', 403: 'Forbidden',
  404: 'Not Found', 405: 'Method Not Allowed', 406: 'Not Acceptable',
  407: 'Proxy Authentication Required', 408: 'Request Timeout', 409: 'Conflict',
  410: 'Gone', 411: 'Length Required', 412: 'Precondition Failed',
  413: 'Payload Too Large', 414: 'URI Too Long', 415: 'Unsupported Media Type',
  416: 'Range Not Satisfiable', 417: 'Expectation Failed', 418: "I'm a Teapot",
  421: 'Misdirected Request', 422: 'Unprocessable Entity', 423: 'Locked',
  424: 'Failed Dependency', 425: 'Too Early', 426: 'Upgrade Required',
  428: 'Precondition Required', 429: 'Too Many Requests',
  431: 'Request Header Fields Too Large', 451: 'Unavailable For Legal Reasons',
  500: 'Internal Server Error', 501: 'Not Implemented', 502: 'Bad Gateway',
  503: 'Service Unavailable', 504: 'Gateway Timeout', 505: 'HTTP Version Not Supported',
  506: 'Variant Also Negotiates', 507: 'Insufficient Storage', 508: 'Loop Detected',
  510: 'Not Extended', 511: 'Network Authentication Required',
};

const MAX_HEADER_SIZE = 16384;
const TOKEN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
const INVALID_HEADER_VALUE = /[^\t\x20-\x7e\x80-\xff]/;
const INVALID_PATH = /[^!-ÿ]/;
const NO_BODY_METHODS = new Set(['GET', 'HEAD', 'DELETE', 'OPTIONS', 'TRACE', 'CONNECT']);

function reportError(err) {
  console.error('Uncaught ' + ((err && err.stack) || err));
}

// Run fn after the current callback returns (V8 runs microtasks then)
function defer(fn) {
  Promise.resolve().then(() => {
    try { fn(); } catch (err) { reportError(err); }
  });
}

function httpError(message, code) {
  const err = new Error(message);
  err.code = code;
  return err;
}

function hasToken(value, token) {
  return value !== undefined &&
    String(value).toLowerCase().split(',').some((part) => part.trim() === token);
}

function isChunked(transferEncoding) {
  const codings = String(transferEncoding).toLowerCase().split(',');
  return codings[codings.length - 1].trim() === 'chunked';
}

// Encode a body chunk as a byte string
function toBytes(chunk, encoding) {
  if (typeof chunk === 'string' && (encoding === 'latin1' || encoding === 'binary')) {
    return chunk;
  }
  return binding.toBytes(chunk);
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

// Decodes UTF-8 across chunk boundaries
class Utf8Decoder {
  constructor() { this.pending = ''; }
  write(bytes) {
    const all = this.pending + bytes;
    const cut = incompleteUtf8Tail(all);
    this.pending = all.slice(cut);
    return cut > 0 ? binding.bytesToUtf8(all.slice(0, cut)) : '';
  }
  end() {
    const rest = this.pending;
    this.pending = '';
    return rest ? binding.bytesToUtf8(rest) : '';
  }
}

// Minimal internal event emitter. Listener exceptions are reported, not thrown,
// so they can't corrupt protocol state.
class EventEmitter {
  constructor() { this._events = Object.create(null); }
  on(name, listener) {
    (this._events[name] || (this._events[name] = [])).push(listener);
    return this;
  }
  addListener(name, listener) { return this.on(name, listener); }
  once(name, listener) {
    const wrapper = (...args) => {
      this.off(name, wrapper);
      listener.apply(this, args);
    };
    wrapper.listener = listener;
    return this.on(name, wrapper);
  }
  off(name, listener) {
    const list = this._events[name];
    if (list) {
      const i = list.findIndex((l) => l === listener || l.listener === listener);
      if (i >= 0) list.splice(i, 1);
    }
    return this;
  }
  removeListener(name, listener) { return this.off(name, listener); }
  removeAllListeners(name) {
    if (name === undefined) this._events = Object.create(null);
    else delete this._events[name];
    return this;
  }
  listenerCount(name) {
    const list = this._events[name];
    return list ? list.length : 0;
  }
  emit(name, ...args) {
    const list = this._events[name];
    if (!list || list.length === 0) {
      if (name === 'error') reportError(args[0]);
      return false;
    }
    for (const listener of list.slice()) {
      try {
        listener.apply(this, args);
      } catch (err) {
        reportError(err);
        if (this._onListenerError) this._onListenerError(err);
      }
    }
    return true;
  }
}

// Parse "Name: value" lines into a headers object and a raw [name, value, ...] list
function parseHeaderLines(lines, start, headers, raw) {
  for (let i = start; i < lines.length; i++) {
    const line = lines[i];
    const colon = line.indexOf(':');
    if (colon <= 0) return false;
    const name = line.slice(0, colon);
    if (!TOKEN.test(name)) return false;
    const value = line.slice(colon + 1).replace(/^[ \t]+|[ \t]+$/g, '');
    if (/[\0\r\n]/.test(value)) return false;
    raw.push(name, value);

    const key = name.toLowerCase();
    if (!(key in headers)) headers[key] = key === 'set-cookie' ? [value] : value;
    else if (key === 'set-cookie') headers[key].push(value);
    else headers[key] += ', ' + value;
  }
  return true;
}

// Incremental HTTP/1.x parser over byte strings.
// handlers: onHeaders(info), onInformation(info), onBody(bytes), onComplete(), onError(err)
class HTTPParser {
  constructor(type, handlers, maxHeaderSize) {
    this.type = type;            // 'request' | 'response'
    this.handlers = handlers;
    this.maxHeaderSize = maxHeaderSize || MAX_HEADER_SIZE;
    this.noBody = false;         // response to a HEAD request
    this.buf = '';
    this.state = 'head';
    this.remaining = 0;
    this.info = null;
    this.paused = false;
    this.running = false;
  }

  execute(data) {
    if (this.state === 'closed' || this.state === 'error') return;
    this.buf += data;
    this._run();
  }

  pause() { this.paused = true; }

  resume() {
    this.paused = false;
    this._run();
  }

  close() {
    this.state = 'closed';
    this.buf = '';
  }

  // The peer closed the connection; completes a close-delimited body
  finish() {
    if (this.state === 'body-eof') {
      if (this.buf) {
        const rest = this.buf;
        this.buf = '';
        this.handlers.onBody(rest);
      }
      this._complete();
    }
  }

  // True while part of a message has been received
  get inMessage() {
    return this.state !== 'head' && this.state !== 'done' && this.state !== 'closed' && this.state !== 'error';
  }

  _run() {
    if (this.running) return;
    this.running = true;
    try {
      while (!this.paused && this._step()) {}
    } finally {
      this.running = false;
    }
  }

  _fail(message, code) {
    this.state = 'error';
    this.buf = '';
    this.handlers.onError(httpError('Parse Error: ' + message, code));
    return false;
  }

  _complete() {
    this.state = this.type === 'request' ? 'head' : 'done';
    this.handlers.onComplete();
  }

  // Deliver up to this.remaining body bytes; returns true on progress
  _readBody(nextState) {
    if (this.buf.length === 0) return false;
    const n = Math.min(this.remaining, this.buf.length);
    const chunk = this.buf.slice(0, n);
    this.buf = this.buf.slice(n);
    this.remaining -= n;
    this.handlers.onBody(chunk);
    if (this.remaining === 0 && this.state !== 'closed') {
      if (nextState) this.state = nextState;
      else this._complete();
    }
    return true;
  }

  _step() {
    switch (this.state) {
      case 'head':
        return this._parseHead();
      case 'body-fixed':
        return this._readBody(null);
      case 'body-eof': {
        if (this.buf.length === 0) return false;
        const chunk = this.buf;
        this.buf = '';
        this.handlers.onBody(chunk);
        return true;
      }
      case 'chunk-size': {
        const end = this.buf.indexOf('\r\n');
        if (end < 0) {
          return this.buf.length > 1024 ? this._fail('Invalid chunk size', 'HPE_INVALID_CHUNK_SIZE') : false;
        }
        const match = /^([0-9A-Fa-f]{1,12})[ \t]*(;.*)?$/.exec(this.buf.slice(0, end));
        if (!match) return this._fail('Invalid chunk size', 'HPE_INVALID_CHUNK_SIZE');
        this.buf = this.buf.slice(end + 2);
        const size = parseInt(match[1], 16);
        if (size === 0) {
          this.state = 'trailers';
        } else {
          this.remaining = size;
          this.state = 'chunk-data';
        }
        return true;
      }
      case 'chunk-data':
        return this._readBody('chunk-end');
      case 'chunk-end':
        if (this.buf.length < 2) return false;
        if (!this.buf.startsWith('\r\n')) return this._fail('Missing CRLF after chunk', 'HPE_STRICT');
        this.buf = this.buf.slice(2);
        this.state = 'chunk-size';
        return true;
      case 'trailers': {
        if (this.buf.startsWith('\r\n')) {
          this.buf = this.buf.slice(2);
          this._complete();
          return true;
        }
        const end = this.buf.indexOf('\r\n\r\n');
        if (end < 0) {
          return this.buf.length > this.maxHeaderSize ? this._fail('Header overflow', 'HPE_HEADER_OVERFLOW') : false;
        }
        const lines = this.buf.slice(0, end).split('\r\n');
        this.buf = this.buf.slice(end + 4);
        if (!parseHeaderLines(lines, 0, this.info.trailers, this.info.rawTrailers)) {
          return this._fail('Invalid trailer', 'HPE_INVALID_HEADER_TOKEN');
        }
        this._complete();
        return true;
      }
      default:
        return false;
    }
  }

  _parseHead() {
    // Ignore empty lines before the start line (RFC 9112 2.2)
    while (this.buf.startsWith('\r\n')) this.buf = this.buf.slice(2);
    if (this.buf.length === 0) return false;

    const end = this.buf.indexOf('\r\n\r\n');
    if (end < 0 || end > this.maxHeaderSize) {
      return this.buf.length > this.maxHeaderSize ? this._fail('Header overflow', 'HPE_HEADER_OVERFLOW') : false;
    }
    const lines = this.buf.slice(0, end).split('\r\n');
    this.buf = this.buf.slice(end + 4);

    const info = this.info = {
      headers: Object.create(null), rawHeaders: [],
      trailers: Object.create(null), rawTrailers: [],
    };

    if (this.type === 'request') {
      const match = /^([!#$%&'*+\-.^_`|~0-9A-Za-z]+) (\S+) HTTP\/(\d)\.(\d)$/.exec(lines[0]);
      if (!match) return this._fail('Invalid request line', 'HPE_INVALID_REQUEST_LINE');
      info.method = match[1];
      info.url = match[2];
      info.versionMajor = Number(match[3]);
      info.versionMinor = Number(match[4]);
      if (info.versionMajor !== 1) return this._fail('Unsupported HTTP version', 'HPE_INVALID_VERSION');
    } else {
      const match = /^HTTP\/(\d)\.(\d) (\d{3})(?: (.*))?$/.exec(lines[0]);
      if (!match) return this._fail('Invalid status line', 'HPE_INVALID_STATUS');
      info.versionMajor = Number(match[1]);
      info.versionMinor = Number(match[2]);
      info.statusCode = Number(match[3]);
      info.statusMessage = match[4] || '';
    }

    if (!parseHeaderLines(lines, 1, info.headers, info.rawHeaders)) {
      return this._fail('Invalid header', 'HPE_INVALID_HEADER_TOKEN');
    }

    // Interim (1xx) responses precede the real one
    if (this.type === 'response' && info.statusCode < 200 && info.statusCode !== 101) {
      this.handlers.onInformation(info);
      return this.state !== 'closed';
    }

    // Work out how the body is framed (RFC 9112 6.3)
    let contentLength = null;
    const lengths = [];
    for (let i = 0; i < info.rawHeaders.length; i += 2) {
      if (info.rawHeaders[i].toLowerCase() === 'content-length') {
        for (const part of info.rawHeaders[i + 1].split(',')) lengths.push(part.trim());
      }
    }
    if (lengths.length > 0) {
      if (!lengths.every((v) => /^\d+$/.test(v) && v === lengths[0]) || !Number.isSafeInteger(Number(lengths[0]))) {
        return this._fail('Invalid Content-Length', 'HPE_INVALID_CONTENT_LENGTH');
      }
      contentLength = Number(lengths[0]);
    }

    const transferEncoding = info.headers['transfer-encoding'];
    let chunked = false;
    if (transferEncoding !== undefined) {
      // Both headers together are a request smuggling vector
      if (contentLength !== null) {
        return this._fail('Content-Length with Transfer-Encoding', 'HPE_UNEXPECTED_CONTENT_LENGTH');
      }
      chunked = isChunked(transferEncoding);
      if (!chunked && this.type === 'request') {
        return this._fail('Invalid Transfer-Encoding', 'HPE_INVALID_TRANSFER_ENCODING');
      }
    }

    let next;
    const status = info.statusCode;
    if (this.type === 'response' && (this.noBody || status === 204 || status === 304)) next = 'complete';
    else if (chunked) next = 'chunk-size';
    else if (contentLength !== null) next = contentLength > 0 ? 'body-fixed' : 'complete';
    else if (this.type === 'request') next = 'complete';
    else next = 'body-eof';

    this.remaining = contentLength || 0;
    this.state = next === 'complete' ? 'headers-done' : next;
    this.handlers.onHeaders(info);
    if (this.state === 'closed') return false;
    if (next === 'complete') this._complete();
    return true;
  }
}

// A received request (server side) or response (client side).
// 'data' delivers UTF-8 strings (byte strings after setEncoding('latin1')).
// Body chunks are buffered until a 'data' listener is attached or resume() is called.
class IncomingMessage extends EventEmitter {
  constructor(socket) {
    super();
    this.socket = socket;
    this.connection = socket;
    this.headers = Object.create(null);
    this.rawHeaders = [];
    this.trailers = Object.create(null);
    this.rawTrailers = [];
    this.httpVersion = '1.1';
    this.httpVersionMajor = 1;
    this.httpVersionMinor = 1;
    this.method = null;
    this.url = '';
    this.statusCode = null;
    this.statusMessage = null;
    this.complete = false;
    this.aborted = false;
    this.readableEnded = false;
    this._chunks = [];
    this._encoding = 'utf8';
    this._decoder = new Utf8Decoder();
    this._flowing = false;
    this._flushScheduled = false;
  }

  _init(info) {
    this.headers = info.headers;
    this.rawHeaders = info.rawHeaders;
    this.trailers = info.trailers;
    this.rawTrailers = info.rawTrailers;
    this.httpVersionMajor = info.versionMajor;
    this.httpVersionMinor = info.versionMinor;
    this.httpVersion = info.versionMajor + '.' + info.versionMinor;
  }

  setEncoding(encoding) {
    this._encoding = encoding === 'latin1' || encoding === 'binary' ? 'latin1' : 'utf8';
    return this;
  }

  on(name, listener) {
    super.on(name, listener);
    if (name === 'data' || name === 'end') this._scheduleFlush();
    return this;
  }

  // Start the flow of data without a 'data' listener (discarding the body)
  resume() {
    this._flowing = true;
    this._scheduleFlush();
    return this;
  }

  _push(bytes) {
    this._chunks.push(bytes);
    this._scheduleFlush();
  }

  _complete() {
    this.complete = true;
    this._scheduleFlush();
  }

  _abort() {
    defer(() => {
      if (this.readableEnded) return;
      this.aborted = true;
      this.emit('aborted');
      this.emit('close');
    });
  }

  _scheduleFlush() {
    if (this._flushScheduled) return;
    this._flushScheduled = true;
    defer(() => {
      this._flushScheduled = false;
      this._flush();
    });
  }

  _flush() {
    if (this.readableEnded || this.aborted) return;
    const latin1 = this._encoding === 'latin1';
    if (this._flowing || this.listenerCount('data') > 0) {
      while (this._chunks.length > 0) {
        const bytes = this._chunks.shift();
        const data = latin1 ? bytes : this._decoder.write(bytes);
        if (data.length > 0) this.emit('data', data);
      }
    }
    if (this.complete && this._chunks.length === 0) {
      const rest = latin1 ? '' : this._decoder.end();
      if (rest) this.emit('data', rest);
      this.readableEnded = true;
      this.emit('end');
      this.emit('close');
    }
  }
}

function validateHeader(name, value) {
  if (typeof name !== 'string' || !TOKEN.test(name)) {
    throw httpError(`Header name must be a valid HTTP token ["${name}"]`, 'ERR_INVALID_HTTP_TOKEN');
  }
  if (value === undefined) {
    throw httpError(`Invalid value "undefined" for header "${name}"`, 'ERR_HTTP_INVALID_HEADER_VALUE');
  }
  for (const v of Array.isArray(value) ? value : [value]) {
    if (INVALID_HEADER_VALUE.test(String(v))) {
      throw httpError(`Invalid character in header content ["${name}"]`, 'ERR_INVALID_CHAR');
    }
  }
}

// Shared header and body handling for ServerResponse and ClientRequest
class OutgoingMessage extends EventEmitter {
  constructor(socket) {
    super();
    this.socket = socket;
    this.connection = socket;
    this.headersSent = false;
    this.finished = false;
    this.writableEnded = false;
    this.writableFinished = false;
    this._headers = Object.create(null);  // lowercase name -> [name, value]
    this._headerWritten = false;
    this._chunked = false;
    this._hasBody = true;
  }

  setHeader(name, value) {
    if (this.headersSent) {
      throw httpError('Cannot set headers after they are sent to the client', 'ERR_HTTP_HEADERS_SENT');
    }
    validateHeader(name, value);
    this._headers[name.toLowerCase()] = [name, Array.isArray(value) ? value.map(String) : String(value)];
    return this;
  }

  getHeader(name) {
    const entry = this._headers[String(name).toLowerCase()];
    return entry && entry[1];
  }

  getHeaders() {
    const headers = Object.create(null);
    for (const key in this._headers) headers[key] = this._headers[key][1];
    return headers;
  }

  getHeaderNames() { return Object.keys(this._headers); }

  hasHeader(name) { return String(name).toLowerCase() in this._headers; }

  removeHeader(name) {
    if (this.headersSent) {
      throw httpError('Cannot remove headers after they are sent to the client', 'ERR_HTTP_HEADERS_SENT');
    }
    delete this._headers[String(name).toLowerCase()];
  }

  _renderHead(startLine) {
    let head = startLine + '\r\n';
    for (const key in this._headers) {
      const [name, value] = this._headers[key];
      for (const v of Array.isArray(value) ? value : [value]) head += name + ': ' + v + '\r\n';
    }
    return head + '\r\n';
  }

  write(chunk, encoding, callback) {
    if (typeof encoding === 'function') {
      callback = encoding;
      encoding = undefined;
    }
    if (this.finished) {
      const err = httpError('write after end', 'ERR_STREAM_WRITE_AFTER_END');
      defer(() => {
        if (callback) callback(err);
        this.emit('error', err);
      });
      return false;
    }
    if (chunk === undefined || chunk === null) {
      throw new TypeError('The "chunk" argument must be of type string or Buffer');
    }
    const bytes = toBytes(chunk, encoding);
    if (!this._headerWritten) this._writeHead(null);
    this._writeBody(bytes);
    if (callback) defer(callback);
    return true;
  }

  end(chunk, encoding, callback) {
    if (typeof chunk === 'function') {
      callback = chunk;
      chunk = undefined;
    } else if (typeof encoding === 'function') {
      callback = encoding;
      encoding = undefined;
    }
    if (this.finished) {
      if (callback) defer(callback);
      return this;
    }
    const bytes = chunk === undefined || chunk === null ? '' : toBytes(chunk, encoding);
    if (!this._headerWritten) this._writeHead(bytes.length);
    this._writeBody(bytes);
    if (this._chunked) this._writeRaw('0\r\n\r\n');
    this.finished = this.writableEnded = true;
    if (callback) this.once('finish', callback);
    this._onEnd();
    return this;
  }

  _writeBody(bytes) {
    if (!this._hasBody || bytes.length === 0) return;
    if (this._chunked) this._writeRaw(bytes.length.toString(16) + '\r\n' + bytes + '\r\n');
    else this._writeRaw(bytes);
  }

  _emitFinish() {
    defer(() => {
      this.writableFinished = true;
      this.emit('finish');
    });
  }
}

class ServerResponse extends OutgoingMessage {
  constructor(req, conn, server) {
    super(conn.socket);
    this.req = req;
    this.statusCode = 200;
    this.statusMessage = undefined;
    this.sendDate = true;
    this._conn = conn;
    this._server = server;
    this._keepAlive = conn.keepAlive;
    this._done = false;
  }

  writeContinue() {
    this._writeRaw('HTTP/1.1 100 Continue\r\n\r\n');
  }

  writeHead(statusCode, statusMessage, headers) {
    if (this.headersSent) {
      throw httpError('Cannot write headers after they are sent to the client', 'ERR_HTTP_HEADERS_SENT');
    }
    if (typeof statusMessage !== 'string') {
      headers = statusMessage;
      statusMessage = undefined;
    }
    this.statusCode = statusCode;
    if (statusMessage !== undefined) this.statusMessage = statusMessage;
    this._validateStatus();
    if (Array.isArray(headers)) {
      for (let i = 0; i + 1 < headers.length; i += 2) this.setHeader(headers[i], headers[i + 1]);
    } else if (headers) {
      for (const name of Object.keys(headers)) {
        if (headers[name] !== undefined) this.setHeader(name, headers[name]);
      }
    }
    // Headers are final now; they go out with the first write()/end()
    this.headersSent = true;
    return this;
  }

  _validateStatus() {
    const code = this.statusCode;
    if (!Number.isInteger(code) || code < 100 || code > 999) {
      throw new RangeError(`Invalid status code: ${code}`);
    }
    if (this.statusMessage !== undefined && /[\r\n]/.test(String(this.statusMessage))) {
      throw httpError('Invalid character in statusMessage', 'ERR_INVALID_CHAR');
    }
  }

  _writeHead(bodyLength) {
    this._validateStatus();
    const code = this.statusCode;
    const message = this.statusMessage !== undefined ? String(this.statusMessage) : (STATUS_CODES[code] || 'unknown');
    const req = this.req;
    const h = this._headers;
    this.headersSent = this._headerWritten = true;

    const bodyless = code === 204 || code === 304 || code < 200;
    this._hasBody = req.method !== 'HEAD' && !bodyless;

    if (this.sendDate && !('date' in h)) h.date = ['Date', new Date().toUTCString()];
    if (hasToken(this.getHeader('connection'), 'close') || !this._server.listening) this._keepAlive = false;

    if (bodyless || 'content-length' in h) {
      // Framing is fixed (or absent)
    } else if ('transfer-encoding' in h) {
      this._chunked = this._hasBody && isChunked(h['transfer-encoding'][1]);
      if (!this._chunked && this._hasBody) this._keepAlive = false;
    } else if (bodyLength !== null) {
      if (req.method !== 'HEAD' || bodyLength > 0) h['content-length'] = ['Content-Length', String(bodyLength)];
    } else if (req.httpVersionMajor === 1 && req.httpVersionMinor >= 1) {
      if (this._hasBody) {
        this._chunked = true;
        h['transfer-encoding'] = ['Transfer-Encoding', 'chunked'];
      }
    } else if (this._hasBody) {
      // HTTP/1.0 client: the body ends when the connection closes
      this._keepAlive = false;
    }

    if (!('connection' in h)) h.connection = ['Connection', this._keepAlive ? 'keep-alive' : 'close'];
    if (this._keepAlive && !('keep-alive' in h) && this._server.keepAliveTimeout > 0) {
      h['keep-alive'] = ['Keep-Alive', 'timeout=' + Math.floor(this._server.keepAliveTimeout / 1000)];
    }
    this._conn.keepAlive = this._keepAlive;
    this._writeRaw(this._renderHead(`HTTP/1.1 ${code} ${message}`));
  }

  _writeRaw(bytes) {
    this.socket.write(bytes, 'latin1');
  }

  _onEnd() {
    this._done = true;
    this._emitFinish();
    defer(() => this.emit('close'));
    this._server._responseDone(this._conn);
  }
}

class Server extends EventEmitter {
  constructor(options, requestListener) {
    super();
    if (typeof options === 'function') {
      requestListener = options;
      options = {};
    }
    options = options || {};
    if (requestListener) this.on('request', requestListener);
    this.listening = false;
    this.keepAliveTimeout = 5000;
    this.headersTimeout = 60000;
    this.maxHeaderSize = options.maxHeaderSize || MAX_HEADER_SIZE;
    this._handle = null;
    this._connections = new Set();
    this._closing = false;
  }

  // listen([port][, host][, callback]) or listen({ port, host }[, callback])
  listen(...args) {
    let port = 0;
    let host;
    let i = 0;
    if (args[0] !== null && typeof args[0] === 'object') {
      port = args[0].port || 0;
      host = args[0].host;
      i = 1;
    } else {
      if (typeof args[0] === 'number' || (typeof args[0] === 'string' && /^\d+$/.test(args[0]))) {
        port = Number(args[0]);
        i = 1;
      }
      if (typeof args[i] === 'string') host = args[i];
    }
    const callback = args.find((a) => typeof a === 'function');

    if (this._handle) throw httpError('Listen method has been called more than once', 'ERR_SERVER_ALREADY_LISTEN');
    if (callback) this.once('listening', callback);

    // allowHalfOpen: a client that half-closes still gets its responses
    const handle = net.createServer({ allowHalfOpen: true }, (socket) => this._onConnection(socket));
    try {
      if (host !== undefined) handle.listen(port, host);
      else handle.listen(port);
    } catch (err) {
      handle.close();
      defer(() => this.emit('error', err));
      return this;
    }
    this._handle = handle;
    this.listening = true;
    defer(() => this.emit('listening'));
    return this;
  }

  address() {
    return this._handle ? this._handle.address() : null;
  }

  // Stop accepting connections; idle ones close now, busy ones after their response
  close(callback) {
    if (!this.listening) {
      if (typeof callback === 'function') {
        defer(() => callback(httpError('Server is not running.', 'ERR_SERVER_NOT_RUNNING')));
      }
      return this;
    }
    if (typeof callback === 'function') this.once('close', callback);
    this.listening = false;
    this._closing = true;
    const handle = this._handle;
    handle.close(() => {
      this._handle = null;
      this._maybeEmitClose();
    });
    this.closeIdleConnections();
    return this;
  }

  closeIdleConnections() {
    for (const conn of this._connections) {
      if (!conn.req) this._endConnection(conn);
    }
  }

  closeAllConnections() {
    for (const conn of this._connections) conn.socket.destroy();
  }

  _maybeEmitClose() {
    if (this._closing && this._handle === null && this._connections.size === 0) {
      this._closing = false;
      this.emit('close');
    }
  }

  _onConnection(socket) {
    socket.setEncoding('latin1');
    const conn = {
      socket, req: null, res: null, reqComplete: false, keepAlive: true,
      timer: null, peerEnded: false, ending: false, closed: false, parser: null,
    };
    conn.parser = new HTTPParser('request', {
      onHeaders: (info) => this._onRequest(conn, info),
      onInformation: () => {},
      onBody: (bytes) => conn.req._push(bytes),
      onComplete: () => {
        conn.reqComplete = true;
        conn.req._complete();
        if (conn.res._done) this._next(conn);
        else conn.parser.pause();
      },
      onError: (err) => this._onParseError(conn, err),
    }, this.maxHeaderSize);
    this._connections.add(conn);
    this._setTimer(conn, this.headersTimeout);

    socket.on('data', (data) => {
      conn.parser.execute(data);
      this._maybeEnd(conn);
    });
    socket.on('end', () => {
      conn.peerEnded = true;
      if (conn.req && !conn.reqComplete) {
        // Request cut short: nothing more can be read
        conn.parser.close();
        conn.req._abort();
        this._endConnection(conn);
        return;
      }
      this._maybeEnd(conn);
    });
    socket.on('error', (err) => {
      if (this.listenerCount('clientError') > 0) this.emit('clientError', err, socket);
    });
    socket.on('close', () => this._onConnectionClose(conn));

    this.emit('connection', socket);
  }

  _onRequest(conn, info) {
    this._clearTimer(conn);
    const req = new IncomingMessage(conn.socket);
    req._init(info);
    req.method = info.method;
    req.url = info.url;

    const connection = info.headers.connection;
    conn.keepAlive = info.versionMinor >= 1 ? !hasToken(connection, 'close') : hasToken(connection, 'keep-alive');

    const res = new ServerResponse(req, conn, this);
    // A throwing handler (or req/res listener) gets a 500 instead of a hung client
    req._onListenerError = res._onListenerError = () => this._failResponse(conn, res);
    conn.req = req;
    conn.res = res;
    conn.reqComplete = false;

    if (String(info.headers.expect || '').toLowerCase() === '100-continue') {
      if (this.listenerCount('checkContinue') > 0) {
        this.emit('checkContinue', req, res);
        return;
      }
      res.writeContinue();
    }

    // Like emit('request'), but stops at the first throwing handler
    const listeners = (this._events.request || []).slice();
    for (const listener of listeners) {
      try {
        listener.call(this, req, res);
      } catch (err) {
        reportError(err);
        this._failResponse(conn, res);
        return;
      }
    }
  }

  _failResponse(conn, res) {
    if (res.finished || conn.closed) return;
    if (!res._headerWritten) {
      res._headers = Object.create(null);
      res._headers.connection = ['Connection', 'close'];
      res.statusCode = 500;
      res.statusMessage = undefined;
      res.end();
    } else {
      conn.socket.destroy();
    }
  }

  // Called when a response has been fully written
  _responseDone(conn) {
    if (conn.reqComplete) this._next(conn);
    // Otherwise the request body is still arriving; onComplete calls _next
  }

  // Both request and response are done: close, or wait for the next request
  _next(conn) {
    conn.req = null;
    conn.res = null;
    conn.reqComplete = false;
    if (conn.closed) return;
    if (!conn.keepAlive || !this.listening) {
      this._endConnection(conn);
      return;
    }
    this._setTimer(conn, this.keepAliveTimeout);
    conn.parser.resume();
    this._maybeEnd(conn);
  }

  // The client has half-closed and nothing is in flight: finish our side
  _maybeEnd(conn) {
    if (conn.peerEnded && !conn.req && !conn.parser.running) this._endConnection(conn);
  }

  _endConnection(conn) {
    if (conn.ending || conn.closed) return;
    conn.ending = true;
    this._clearTimer(conn);
    conn.parser.close();
    conn.socket.end();
  }

  _onParseError(conn, err) {
    if (this.listenerCount('clientError') > 0) {
      this.emit('clientError', err, conn.socket);
      return;
    }
    if (conn.res && conn.res._headerWritten) {
      conn.socket.destroy();
      return;
    }
    const status = err.code === 'HPE_HEADER_OVERFLOW' ? 431 : err.code === 'HPE_INVALID_VERSION' ? 505 : 400;
    conn.socket.write(`HTTP/1.1 ${status} ${STATUS_CODES[status]}\r\nConnection: close\r\n\r\n`, 'latin1');
    this._endConnection(conn);
  }

  _onConnectionClose(conn) {
    conn.closed = true;
    this._clearTimer(conn);
    conn.parser.close();
    this._connections.delete(conn);
    if (conn.req && !conn.reqComplete) conn.req._abort();
    if (conn.res && !conn.res._done) defer(() => conn.res.emit('close'));
    this._maybeEmitClose();
  }

  _setTimer(conn, ms) {
    this._clearTimer(conn);
    if (!(ms > 0)) return;
    conn.timer = setTimeout(() => {
      conn.timer = null;
      if (conn.req) return;
      if (conn.parser.buf.length > 0) {
        conn.socket.write('HTTP/1.1 408 Request Timeout\r\nConnection: close\r\n\r\n', 'latin1');
      }
      this._endConnection(conn);
    }, ms);
  }

  _clearTimer(conn) {
    if (conn.timer !== null) {
      clearTimeout(conn.timer);
      conn.timer = null;
    }
  }
}

class ClientRequest extends OutgoingMessage {
  constructor(options, callback) {
    super(null);
    const protocol = options.protocol || 'http:';
    if (protocol !== 'http:') {
      throw httpError(`Protocol "${protocol}" not supported. Expected "http:"`, 'ERR_INVALID_PROTOCOL');
    }

    let host = options.hostname || options.host || 'localhost';
    if (host[0] === '[' && host[host.length - 1] === ']') host = host.slice(1, -1);
    const port = options.port === undefined || options.port === null || options.port === ''
      ? 80 : Number(options.port);
    if (!Number.isInteger(port) || port < 0 || port > 65535) {
      throw new RangeError(`Invalid port: ${options.port}`);
    }
    const method = String(options.method || 'GET').toUpperCase();
    if (!TOKEN.test(method)) throw httpError(`Method must be a valid HTTP token ["${method}"]`, 'ERR_INVALID_HTTP_TOKEN');
    const path = options.path || '/';
    if (INVALID_PATH.test(path)) throw httpError('Request path contains unescaped characters', 'ERR_UNESCAPED_CHARACTERS');

    this.method = method;
    this.path = path;
    this.host = host;
    this.port = port;
    this.protocol = protocol;
    this.aborted = false;
    this.destroyed = false;
    this.res = null;
    this._pending = [];       // raw writes queued until connected
    this._timeout = 0;
    this._timer = null;
    this._errored = false;
    this._responseDone = false;
    this._userDestroyed = false;

    const hostHeader = (host.includes(':') ? `[${host}]` : host) + (port !== 80 ? ':' + port : '');
    this._headers.host = ['Host', hostHeader];
    const headers = options.headers;
    if (Array.isArray(headers)) {
      for (let i = 0; i + 1 < headers.length; i += 2) this.setHeader(headers[i], headers[i + 1]);
    } else if (headers) {
      for (const name of Object.keys(headers)) {
        if (headers[name] !== undefined) this.setHeader(name, headers[name]);
      }
    }

    if (callback) this.once('response', callback);
    if (options.timeout) this.setTimeout(options.timeout);
    this._connect();
  }

  _connect() {
    let socket;
    try {
      socket = net.connect(this.port, this.host);
    } catch (err) {
      defer(() => {
        this._fail(err);
        this.emit('close');
      });
      return;
    }
    this.socket = this.connection = socket;
    socket.setEncoding('latin1');

    const parser = new HTTPParser('response', {
      onHeaders: (info) => {
        const res = new IncomingMessage(socket);
        res._init(info);
        res.statusCode = info.statusCode;
        res.statusMessage = info.statusMessage;
        res.req = this;
        this.res = res;
        this.emit('response', res);
      },
      onInformation: (info) => {
        this.emit('information', {
          statusCode: info.statusCode, statusMessage: info.statusMessage,
          headers: info.headers, rawHeaders: info.rawHeaders,
          httpVersion: info.versionMajor + '.' + info.versionMinor,
        });
      },
      onBody: (bytes) => this.res._push(bytes),
      onComplete: () => {
        this._responseDone = true;
        this._clearTimer();
        this.res._complete();
        socket.end();
      },
      onError: (err) => {
        this._fail(err);
        socket.destroy();
      },
    });
    parser.noBody = this.method === 'HEAD';

    socket.on('connect', () => {
      const pending = this._pending;
      this._pending = null;
      for (const bytes of pending) socket.write(bytes, 'latin1');
      this._touch();
    });
    socket.on('data', (data) => {
      this._touch();
      parser.execute(data);
    });
    socket.on('end', () => parser.finish());
    socket.on('error', (err) => {
      this._socketError = err;
    });
    socket.on('close', () => {
      parser.close();
      this._onClose();
    });
  }

  _writeRaw(bytes) {
    if (this._pending) this._pending.push(bytes);
    else if (this.socket) this.socket.write(bytes, 'latin1');
  }

  _writeHead(bodyLength) {
    const h = this._headers;
    this.headersSent = this._headerWritten = true;
    // One request per connection: there is no connection pooling (Agent)
    if (!('connection' in h)) h.connection = ['Connection', 'close'];
    if ('content-length' in h) {
      // Caller-provided length
    } else if ('transfer-encoding' in h) {
      this._chunked = isChunked(h['transfer-encoding'][1]);
    } else if (bodyLength !== null) {
      if (bodyLength > 0 || !NO_BODY_METHODS.has(this.method)) {
        h['content-length'] = ['Content-Length', String(bodyLength)];
      }
    } else {
      this._chunked = true;
      h['transfer-encoding'] = ['Transfer-Encoding', 'chunked'];
    }
    this._writeRaw(this._renderHead(`${this.method} ${this.path} HTTP/1.1`));
  }

  _onEnd() {
    this._emitFinish();
  }

  _fail(err) {
    if (this._errored) return;
    this._errored = true;
    this.emit('error', err);
  }

  _onClose() {
    this._clearTimer();
    this.destroyed = true;
    if (!this._responseDone) {
      if (this.res) {
        this.res._abort();
      } else if (!this._userDestroyed) {
        this._fail(this._socketError || httpError('socket hang up', 'ECONNRESET'));
      }
    }
    this.emit('close');
  }

  // Emit 'timeout' after ms of socket inactivity (the request is not aborted)
  setTimeout(ms, callback) {
    if (callback) this.once('timeout', callback);
    this._timeout = ms;
    this._touch();
    return this;
  }

  _touch() {
    this._clearTimer();
    if (this._timeout > 0 && !this.destroyed && !this._responseDone) {
      this._timer = setTimeout(() => {
        this._timer = null;
        this.emit('timeout');
      }, this._timeout);
    }
  }

  _clearTimer() {
    if (this._timer !== null) {
      clearTimeout(this._timer);
      this._timer = null;
    }
  }

  abort() {
    if (this.aborted) return;
    this.aborted = true;
    defer(() => this.emit('abort'));
    this.destroy();
  }

  destroy(err) {
    if (this._userDestroyed || this.destroyed) return this;
    this._userDestroyed = true;
    if (err) this._fail(err);
    if (this.socket) this.socket.destroy();
    return this;
  }
}

// Split an http:// URL into request options
function parseURL(input) {
  const match = /^([a-zA-Z][a-zA-Z0-9+.-]*:)\/\/(?:[^@\/?#]*@)?(\[[^\]]*\]|[^:\/?#]*)(?::(\d*))?([^#]*)/.exec(input.trim());
  if (!match || !match[2]) throw httpError(`Invalid URL: ${input}`, 'ERR_INVALID_URL');
  let path = match[4] || '/';
  if (path[0] === '?') path = '/' + path;
  return {
    protocol: match[1].toLowerCase(),
    hostname: match[2],
    port: match[3] ? Number(match[3]) : undefined,
    path,
  };
}

// request(url[, options][, callback]) or request(options[, callback])
function request(url, options, callback) {
  if (typeof url === 'string') {
    if (typeof options === 'function') {
      callback = options;
      options = undefined;
    }
    options = Object.assign(parseURL(url), options);
  } else {
    callback = options;
    options = Object.assign({}, url);
  }
  return new ClientRequest(options, callback);
}

function get(url, options, callback) {
  const req = request(url, options, callback);
  req.end();
  return req;
}

function createServer(options, requestListener) {
  return new Server(options, requestListener);
}

return {
  METHODS, STATUS_CODES, maxHeaderSize: MAX_HEADER_SIZE,
  createServer, request, get,
  Server, IncomingMessage, ServerResponse, ClientRequest, OutgoingMessage,
};
})
)JS";

// toBytes(value): UTF-8 encode a string, or copy a Buffer's raw bytes, into a byte string
static void ToBytes(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() > 0 && IsBuffer(args[0])) {
        BufferData* data = GetBufferData(args[0].As<Object>());
        if (data == nullptr || data->data == nullptr || data->length == 0) {
            args.GetReturnValue().Set(String::Empty(isolate));
            return;
        }
        args.GetReturnValue().Set(String::NewFromOneByte(isolate, (const uint8_t*)data->data,
            NewStringType::kNormal, data->length).ToLocalChecked());
        return;
    }

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate,
            "The \"chunk\" argument must be of type string or Buffer").ToLocalChecked()));
        return;
    }

    String::Utf8Value utf8(isolate, args[0]);
    args.GetReturnValue().Set(String::NewFromOneByte(isolate, (const uint8_t*)*utf8,
        NewStringType::kNormal, utf8.length()).ToLocalChecked());
}

// bytesToUtf8(bytes): decode a byte string as UTF-8 (invalid sequences become U+FFFD)
static void BytesToUtf8(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    Local<String> str;
    if (args.Length() < 1 || !args[0]->ToString(context).ToLocal(&str) || str->Length() == 0) {
        args.GetReturnValue().Set(String::Empty(isolate));
        return;
    }

    std::vector<uint8_t> bytes(str->Length());
    frogjs::WriteOneByte(isolate, str, bytes.data());
    args.GetReturnValue().Set(String::NewFromUtf8(isolate, (const char*)bytes.data(),
        NewStringType::kNormal, bytes.size()).ToLocalChecked());
}

static void ReportSetupError(Isolate* isolate, TryCatch& try_catch) {
    String::Utf8Value error(isolate, try_catch.Exception());
    fprintf(stderr, "Failed to initialize http module: %s\n", *error);
}

// Set up http module (requires SetupNet and SetupTimers first)
void SetupHTTP(Isolate* isolate, Local<Context> context) {
    Local<Object> global = context->Global();

    Local<Object> binding = Object::New(isolate);
    binding->Set(context, String::NewFromUtf8(isolate, "toBytes").ToLocalChecked(),
        FunctionTemplate::New(isolate, ToBytes)->GetFunction(context).ToLocalChecked()).Check();
    binding->Set(context, String::NewFromUtf8(isolate, "bytesToUtf8").ToLocalChecked(),
        FunctionTemplate::New(isolate, BytesToUtf8)->GetFunction(context).ToLocalChecked()).Check();

    TryCatch try_catch(isolate);
    Local<String> source = String::NewFromUtf8(isolate, kHttpSource).ToLocalChecked();
    ScriptOrigin origin(String::NewFromUtf8(isolate, "frogjs:http").ToLocalChecked());

    Local<Script> script;
    Local<Value> factory;
    if (!Script::Compile(context, source, &origin).ToLocal(&script) ||
        !script->Run(context).ToLocal(&factory) || !factory->IsFunction()) {
        ReportSetupError(isolate, try_catch);
        return;
    }

    // Capture the built-ins now so user code replacing the globals can't break http
    Local<Value> argv[4] = {
        global->Get(context, String::NewFromUtf8(isolate, "net").ToLocalChecked()).ToLocalChecked(),
        binding,
        global->Get(context, String::NewFromUtf8(isolate, "setTimeout").ToLocalChecked()).ToLocalChecked(),
        global->Get(context, String::NewFromUtf8(isolate, "clearTimeout").ToLocalChecked()).ToLocalChecked(),
    };

    Local<Value> http;
    if (!factory.As<Function>()->Call(context, Undefined(isolate), 4, argv).ToLocal(&http)) {
        ReportSetupError(isolate, try_catch);
        return;
    }

    // Attach http to global
    global->Set(context, String::NewFromUtf8(isolate, "http").ToLocalChecked(), http).Check();
}
