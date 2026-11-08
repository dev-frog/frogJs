#include <v8.h>
#include <uv.h>
#include "frogjs/allocator.h"
#include "frogjs/buffer.h"
#include "frogjs/encoding.h"
#include "frogjs/errors.h"
#include "frogjs/js_module.h"
#include "frogjs/loop.h"
#include "frogjs/v8_compat.h"
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace v8;

// Sockets are Duplex streams and servers are EventEmitters, in JavaScript, over
// a small native TCP handle. Reads stop when the stream's buffer is full, and a
// write's callback runs when the kernel has the data, so backpressure works.
static const char* kNetSource = R"JS(
(function (native, stream, events) {
'use strict';

function nextTick(fn) {
  Promise.resolve().then(fn);
}

const { Duplex } = stream;
const EventEmitter = events;
const kHandle = Symbol('handle');
const kServer = Symbol('server');

function makeError(Ctor, code, message) {
  const err = new Ctor(message);
  err.code = code;
  return err;
}

// connect ECONNREFUSED 127.0.0.1:80, as in Node.js
function uvError(status, syscall, address, port) {
  const name = native.errname(status);
  let message = `${syscall} ${name}`;
  if (address) message += ` ${address}${port ? ':' + port : ''}`;
  const err = new Error(message);
  err.errno = status;
  err.code = name;
  err.syscall = syscall;
  if (address) err.address = address;
  if (port) err.port = port;
  return err;
}

function isIPv4(value) {
  if (typeof value !== 'string') return false;
  const parts = value.split('.');
  return parts.length === 4 && parts.every((p) => /^(0|[1-9][0-9]{0,2})$/.test(p) && Number(p) <= 255);
}

function isIPv6(value) {
  return typeof value === 'string' && value.includes(':') && native.parseIPv6(value);
}

function isIP(value) {
  if (isIPv4(value)) return 4;
  if (isIPv6(value)) return 6;
  return 0;
}

function checkPort(port) {
  const value = typeof port === 'string' && port !== '' ? Number(port) : port;
  if (typeof value !== 'number' || !Number.isInteger(value) || value < 0 || value > 65535) {
    const err = new RangeError(`Port should be >= 0 and < 65536. Received ${typeof port === 'string' ? `'${port}'` : String(port)}.`);
    err.code = 'ERR_SOCKET_BAD_PORT';
    throw err;
  }
  return value;
}

// Addresses of a host, IPv4 first (the default listen address is IPv4)
function resolve(host, callback) {
  if (typeof host !== 'string') {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "host" argument must be of type string. Received ${typeof host}`);
  }
  // C strings end at a null byte: 'evil.example\0.good.example' must not resolve as evil.example
  if (host.includes('\0')) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE', "The argument 'host' must be a string without null bytes");
  }
  if (isIP(host)) {
    nextTick(() => callback(null, [host]));
    return;
  }
  native.lookup(host, (status, addresses) => {
    if (status) {
      const err = uvError(status, 'getaddrinfo', host);
      err.hostname = host;
      callback(err);
      return;
    }
    callback(null, addresses.sort((a, b) => isIP(a) - isIP(b)));
  });
}

// connect([options][, listener]), connect(port[, host][, listener])
function connectArgs(args) {
  const list = Array.from(args);
  const callback = typeof list[list.length - 1] === 'function' ? list.pop() : undefined;
  let options;
  if (list[0] !== null && typeof list[0] === 'object') options = Object.assign({}, list[0]);
  else options = { port: list[0], host: list[1] };
  return { options, callback };
}

class Socket extends Duplex {
  constructor(options = {}) {
    super({
      allowHalfOpen: !!options.allowHalfOpen,
      highWaterMark: options.highWaterMark,
      emitClose: false,
      decodeStrings: false,
    });
    this[kHandle] = options.handle || null;
    this[kServer] = null;
    this.connecting = false;
    this.bytesRead = 0;
    this.bytesWritten = 0;
    this.remoteAddress = undefined;
    this.remotePort = undefined;
    this.remoteFamily = undefined;
    this._hadError = false;
    this._reading = false;
    this._timer = null;
    this._timeoutMs = 0;
    this._unrefed = false;
    this.on('end', () => {
      if (!this.allowHalfOpen && !this.writableEnded) this.end();
    });
    if (this[kHandle]) {
      this._attach();
      // Start reading, so the end of the connection is noticed without a consumer
      this.read(0);
    }
  }

  _attach() {
    const handle = this[kHandle];
    handle.onread = (status, data) => this._onRead(status, data);
    this._updatePeer();
    if (this._unrefed) native.setRef(handle, false);
  }

  _updatePeer() {
    const peer = native.getpeername(this[kHandle]);
    if (peer) {
      this.remoteAddress = peer.address;
      this.remotePort = peer.port;
      this.remoteFamily = peer.family;
    }
  }

  connect(...args) {
    const { options, callback } = connectArgs(args);
    if (this.connecting || this[kHandle]) {
      throw makeError(Error, 'ERR_SOCKET_ALREADY_CONNECTED', 'Socket is already connected');
    }
    const port = checkPort(options.port);
    const host = options.host || 'localhost';
    if (callback) this.once('connect', callback);
    this.connecting = true;
    resolve(host, (err, addresses) => {
      if (this.destroyed) return;
      if (err) {
        this.connecting = false;
        this.destroy(err);
        return;
      }
      this._connectTo(addresses, 0, port);
    });
    return this;
  }

  // Try the addresses in order until one connects
  _connectTo(addresses, index, port) {
    const address = addresses[index];
    const handle = native.newTcp();
    this[kHandle] = handle;
    if (this._unrefed) native.setRef(handle, false);
    const failed = (status) => {
      if (index + 1 < addresses.length && !this.destroyed) {
        this[kHandle] = null;
        native.close(handle, () => this._connectTo(addresses, index + 1, port));
        return;
      }
      this.connecting = false;
      this.destroy(uvError(status, 'connect', address, port));
    };
    const status = native.connect(handle, address, port, (result) => {
      if (this.destroyed) return;
      if (result) return failed(result);
      this.connecting = false;
      this._attach();
      this.emit('connect');
      this.emit('ready');
      // A read requested while connecting is still waiting for its _read()
      if (this._readableState.reading) this._read();
      else if (!this.isPaused()) this.read(0);
      this._touch();
    });
    if (status) nextTick(() => failed(status));
  }

  _onRead(status, data) {
    this._touch();
    if (status === 0) {
      this.bytesRead += data.length;
      if (!this.push(data) && this[kHandle]) {
        this._reading = false;
        native.readStop(this[kHandle]);
      }
      return;
    }
    this._reading = false;
    if (status === native.UV_EOF) {
      this.push(null);
      // 'end' comes even if nothing consumes the stream
      this.read(0);
      return;
    }
    this.destroy(uvError(status, 'read'));
  }

  _read() {
    if (this.connecting || !this[kHandle] || this._reading) return;
    this._reading = true;
    const status = native.readStart(this[kHandle]);
    if (status) this.destroy(uvError(status, 'read'));
  }

  _write(chunk, encoding, callback) {
    if (this.connecting) {
      const retry = () => {
        this.removeListener('close', cancel);
        this._write(chunk, encoding, callback);
      };
      const cancel = () => {
        this.removeListener('connect', retry);
        callback(makeError(Error, 'ERR_SOCKET_CLOSED', 'Socket is closed'));
      };
      this.once('connect', retry);
      this.once('close', cancel);
      return;
    }
    if (!this[kHandle]) {
      callback(makeError(Error, 'ERR_SOCKET_CLOSED', 'Socket is closed'));
      return;
    }
    this._touch();
    const status = native.write(this[kHandle], chunk, encoding, (result, bytes) => {
      this.bytesWritten += bytes;
      this._touch();
      if (result && result !== native.UV_ECANCELED) callback(uvError(result, 'write'));
      else if (result) callback(makeError(Error, 'ERR_SOCKET_CLOSED', 'Socket is closed'));
      else callback();
    });
    if (status < 0) {
      callback(uvError(status, 'write'));
    } else if (status > 0) {
      if (chunk.length > 0) this.bytesWritten += status;
      callback();
    }
  }

  _final(callback) {
    if (this.connecting) {
      const retry = () => {
        this.removeListener('close', cancel);
        this._final(callback);
      };
      const cancel = () => {
        this.removeListener('connect', retry);
        callback();
      };
      this.once('connect', retry);
      this.once('close', cancel);
      return;
    }
    if (!this[kHandle]) return callback();
    const status = native.shutdown(this[kHandle], () => callback());
    if (status) callback();
  }

  _destroy(err, callback) {
    this.connecting = false;
    this._clearTimer();
    this._hadError = !!err;
    const handle = this[kHandle];
    const finish = () => {
      const server = this[kServer];
      this[kServer] = null;
      this.emit('close', this._hadError);
      if (server) server._connectionClosed();
    };
    // 'error' comes first; 'close' once the handle has really closed
    callback(err);
    if (!handle) {
      nextTick(finish);
      return;
    }
    this[kHandle] = null;
    native.close(handle, finish);
  }

  // Remaining data is sent first (a normal end). resetAndDestroy() isn't supported.
  destroySoon() {
    if (this.writable) this.end();
    if (this.writableFinished) this.destroy();
    else this.once('finish', () => this.destroy());
  }

  setTimeout(ms, callback) {
    this._timeoutMs = ms;
    this._clearTimer();
    if (ms > 0 && callback) this.once('timeout', callback);
    else if (callback) this.removeListener('timeout', callback);
    this._touch();
    return this;
  }

  get timeout() {
    return this._timeoutMs;
  }

  _clearTimer() {
    if (this._timer !== null) {
      clearTimeout(this._timer);
      this._timer = null;
    }
  }

  // Any activity restarts the idle timer
  _touch() {
    if (this._timeoutMs <= 0) return;
    this._clearTimer();
    this._timer = setTimeout(() => {
      this._timer = null;
      this.emit('timeout');
    }, this._timeoutMs);
    this._timer.unref();
  }

  setNoDelay(enable = true) {
    if (this[kHandle]) native.setNoDelay(this[kHandle], !!enable);
    return this;
  }

  setKeepAlive(enable = false, delay = 0) {
    if (this[kHandle]) native.setKeepAlive(this[kHandle], !!enable, Math.floor(delay / 1000));
    return this;
  }

  address() {
    return (this[kHandle] && native.getsockname(this[kHandle])) || {};
  }

  get localAddress() {
    return this.address().address;
  }

  get localPort() {
    return this.address().port;
  }

  get localFamily() {
    return this.address().family;
  }

  get pending() {
    return !this[kHandle] || this.connecting;
  }

  get readyState() {
    if (this.connecting) return 'opening';
    if (this.readable && this.writable) return 'open';
    if (this.readable) return 'readOnly';
    if (this.writable) return 'writeOnly';
    return 'closed';
  }

  ref() {
    this._unrefed = false;
    if (this[kHandle]) native.setRef(this[kHandle], true);
    return this;
  }

  unref() {
    this._unrefed = true;
    if (this[kHandle]) native.setRef(this[kHandle], false);
    return this;
  }
}

class Server extends EventEmitter {
  constructor(options, connectionListener) {
    super();
    if (typeof options === 'function') {
      connectionListener = options;
      options = {};
    } else if (options === null || typeof options !== 'object') {
      options = {};
    }
    if (typeof connectionListener === 'function') this.on('connection', connectionListener);
    this.allowHalfOpen = !!options.allowHalfOpen;
    this.maxConnections = undefined;
    this.listening = false;
    this._handle = null;
    this._connections = 0;
    this._closing = false;
    this._unrefed = false;
  }

  // listen(port[, host][, backlog][, callback]), listen({ port, host, backlog }[, callback])
  listen(...args) {
    const list = args.slice();
    const callback = typeof list[list.length - 1] === 'function' ? list.pop() : undefined;
    let options;
    if (list[0] !== null && typeof list[0] === 'object') options = list[0];
    else {
      options = { port: list[0] };
      if (typeof list[1] === 'string') {
        options.host = list[1];
        options.backlog = list[2];
      } else {
        options.backlog = list[1];
      }
    }
    if (this._handle) {
      throw makeError(Error, 'ERR_SERVER_ALREADY_LISTEN', 'Listen method has been called more than once without closing.');
    }
    const port = options.port === undefined || options.port === null ? 0 : checkPort(options.port);
    const host = options.host || '0.0.0.0';
    const backlog = typeof options.backlog === 'number' ? options.backlog : 511;
    if (callback) this.once('listening', callback);

    // The handle exists from now on, so a second listen() is rejected
    this._handle = native.newTcp();
    if (this._unrefed) native.setRef(this._handle, false);
    resolve(host, (err, addresses) => {
      if (!this._handle) return;
      if (err) return this._listenFailed(err);
      const address = addresses[0];
      const status = native.listen(this._handle, address, port, backlog, (result, client) => this._onConnection(result, client));
      if (status) return this._listenFailed(uvError(status, 'listen', address, port));
      this.listening = true;
      this.emit('listening');
    });
    return this;
  }

  _listenFailed(err) {
    const handle = this._handle;
    this._handle = null;
    native.close(handle, () => this.emit('error', err));
  }

  _onConnection(status, client) {
    if (status) {
      this.emit('error', uvError(status, 'accept'));
      return;
    }
    if (this.maxConnections !== undefined && this._connections >= this.maxConnections) {
      native.close(client, () => {});
      this.emit('drop', {});
      return;
    }
    const socket = new Socket({ handle: client, allowHalfOpen: this.allowHalfOpen });
    socket[kServer] = this;
    this._connections++;
    this.emit('connection', socket);
  }

  _connectionClosed() {
    this._connections--;
    this._maybeEmitClose();
  }

  _maybeEmitClose() {
    if (this._closing && !this._handle && this._connections === 0) {
      this._closing = false;
      nextTick(() => this.emit('close'));
    }
  }

  // Stop accepting connections; 'close' follows when the existing ones have ended
  close(callback) {
    if (typeof callback === 'function') {
      if (!this._handle) {
        this.once('close', () => callback(makeError(Error, 'ERR_SERVER_NOT_RUNNING', 'Server is not running.')));
        nextTick(() => this.emit('close'));
        return this;
      }
      this.once('close', callback);
    }
    if (this._handle) {
      const handle = this._handle;
      this._handle = null;
      this.listening = false;
      this._closing = true;
      native.close(handle, () => this._maybeEmitClose());
    }
    return this;
  }

  address() {
    return this._handle && this.listening ? native.getsockname(this._handle) : null;
  }

  getConnections(callback) {
    nextTick(() => callback(null, this._connections));
    return this;
  }

  ref() {
    this._unrefed = false;
    if (this._handle) native.setRef(this._handle, true);
    return this;
  }

  unref() {
    this._unrefed = true;
    if (this._handle) native.setRef(this._handle, false);
    return this;
  }
}

function createServer(options, connectionListener) {
  return new Server(options, connectionListener);
}

function connect(...args) {
  const { options, callback } = connectArgs(args);
  const socket = new Socket(options);
  if (options.timeout) socket.setTimeout(options.timeout);
  return callback ? socket.connect(options, callback) : socket.connect(options);
}

return {
  Socket, Server, createServer, connect, createConnection: connect,
  isIP, isIPv4, isIPv6,
};
})
)JS";

namespace {

struct TcpWrap {
    uv_tcp_t handle;
    Global<Object> object;
    Global<Function> on_connection;
    Global<Function> on_close;
    Isolate* isolate;
    bool silent;  // the thread is ending: no more JavaScript
};

struct WriteReq {
    uv_write_t req;
    Global<Function> callback;
    Isolate* isolate;
    TcpWrap* wrap;
    size_t total;       // the whole chunk, including what was written right away
    std::string data;   // what wasn't
};

struct ConnectReq {
    uv_connect_t req;
    Global<Function> callback;
    TcpWrap* wrap;
};

struct ShutdownReq {
    uv_shutdown_t req;
    Global<Function> callback;
    TcpWrap* wrap;
};

struct LookupReq {
    uv_getaddrinfo_t req;
    Global<Function> callback;
    Isolate* isolate;
};

// Per thread: the handle template (also how native code recognizes handles),
// the open handles, and one read buffer (libuv delivers each read before
// asking for the next buffer, and the data is copied into a Buffer at once)
thread_local Global<FunctionTemplate> tls_tcp_template;
thread_local std::set<TcpWrap*> tls_wraps;
thread_local bool tls_cleanup_registered = false;
thread_local char tls_read_buffer[64 * 1024];

TcpWrap* Unwrap(Local<Value> value) {
    Isolate* isolate = Isolate::GetCurrent();
    if (tls_tcp_template.IsEmpty() || !value->IsObject() || !tls_tcp_template.Get(isolate)->HasInstance(value)) {
        return nullptr;
    }
    Local<Value> field = value.As<Object>()->GetInternalField(0).As<Value>();
    return field->IsExternal() ? frogjs::ExternalValue<TcpWrap>(field) : nullptr;
}

Local<Object> NewTcpObject(Isolate* isolate, Local<Context> context, TcpWrap** out) {
    Local<Object> object = tls_tcp_template.Get(isolate)->InstanceTemplate()->NewInstance(context).ToLocalChecked();
    TcpWrap* wrap = new TcpWrap;
    wrap->isolate = isolate;
    wrap->silent = false;
    wrap->object.Reset(isolate, object);
    uv_tcp_init(frogjs::Loop(), &wrap->handle);
    wrap->handle.data = wrap;
    object->SetInternalField(0, frogjs::NewExternal(isolate, wrap));
    tls_wraps.insert(wrap);
    *out = wrap;
    return object;
}

void NewTcp(const FunctionCallbackInfo<Value>& args) {
    TcpWrap* wrap;
    args.GetReturnValue().Set(NewTcpObject(args.GetIsolate(), args.GetIsolate()->GetCurrentContext(), &wrap));
}

bool ParseAddress(const std::string& ip, int port, sockaddr_storage* addr) {
    if (ip.find(':') != std::string::npos) {
        return uv_ip6_addr(ip.c_str(), port, reinterpret_cast<sockaddr_in6*>(addr)) == 0;
    }
    return uv_ip4_addr(ip.c_str(), port, reinterpret_cast<sockaddr_in*>(addr)) == 0;
}

std::string ToStdString(Isolate* isolate, Local<Value> value) {
    String::Utf8Value utf8(isolate, value);
    return *utf8 != nullptr ? std::string(*utf8, utf8.length()) : std::string();
}

int ToInt(const FunctionCallbackInfo<Value>& args, int index) {
    return args[index]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
}

// Call a JS callback unless the thread is ending. The caller has a HandleScope.
void Invoke(Isolate* isolate, Local<Function> callback, Local<Value> receiver, int argc, Local<Value> argv[]) {
    (void)frogjs::MakeCallback(isolate, isolate->GetCurrentContext(), callback, receiver, argc, argv);  // reports its own errors
}

void OnConnection(uv_stream_t* server, int status) {
    TcpWrap* wrap = static_cast<TcpWrap*>(server->data);
    Isolate* isolate = wrap->isolate;
    if (wrap->silent) return;
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    Local<Value> argv[2] = {Integer::New(isolate, status), Undefined(isolate)};
    if (status == 0) {
        TcpWrap* client;
        Local<Object> object = NewTcpObject(isolate, context, &client);
        int result = uv_accept(server, reinterpret_cast<uv_stream_t*>(&client->handle));
        argv[0] = Integer::New(isolate, result);
        argv[1] = object;
    }
    Invoke(isolate, wrap->on_connection.Get(isolate), wrap->object.Get(isolate), 2, argv);
}

// listen(handle, ip, port, backlog, callback) -> status
void Listen(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    TcpWrap* wrap = Unwrap(args[0]);
    if (wrap == nullptr) return args.GetReturnValue().Set(UV_EBADF);

    sockaddr_storage addr;
    if (!ParseAddress(ToStdString(isolate, args[1]), ToInt(args, 2), &addr)) return args.GetReturnValue().Set(UV_EINVAL);
    int status = uv_tcp_bind(&wrap->handle, reinterpret_cast<const sockaddr*>(&addr), 0);
    if (status == 0) {
        wrap->on_connection.Reset(isolate, args[4].As<Function>());
        status = uv_listen(reinterpret_cast<uv_stream_t*>(&wrap->handle), ToInt(args, 3), OnConnection);
    }
    args.GetReturnValue().Set(status);
}

// connect(handle, ip, port, callback) -> status; callback(status)
void Connect(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    TcpWrap* wrap = Unwrap(args[0]);
    if (wrap == nullptr) return args.GetReturnValue().Set(UV_EBADF);

    sockaddr_storage addr;
    if (!ParseAddress(ToStdString(isolate, args[1]), ToInt(args, 2), &addr)) return args.GetReturnValue().Set(UV_EINVAL);

    ConnectReq* req = new ConnectReq;
    req->wrap = wrap;
    req->callback.Reset(isolate, args[3].As<Function>());
    req->req.data = req;
    int status = uv_tcp_connect(&req->req, &wrap->handle, reinterpret_cast<const sockaddr*>(&addr), [](uv_connect_t* r, int result) {
        ConnectReq* req = static_cast<ConnectReq*>(r->data);
        Isolate* isolate = req->wrap->isolate;
        if (!req->wrap->silent) {
            HandleScope handle_scope(isolate);
            Local<Value> argv[1] = {Integer::New(isolate, result)};
            Invoke(isolate, req->callback.Get(isolate), Undefined(isolate), 1, argv);
        }
        delete req;
    });
    if (status != 0) delete req;
    args.GetReturnValue().Set(status);
}

void OnAlloc(uv_handle_t* /*handle*/, size_t /*suggested*/, uv_buf_t* buf) {
    buf->base = tls_read_buffer;
    buf->len = sizeof(tls_read_buffer);
}

void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    if (nread == 0) return;  // nothing read (EAGAIN): not an event
    TcpWrap* wrap = static_cast<TcpWrap*>(stream->data);
    Isolate* isolate = wrap->isolate;
    if (wrap->silent) return;
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> object = wrap->object.Get(isolate);

    Local<Value> onread;
    if (!object->Get(context, String::NewFromUtf8Literal(isolate, "onread")).ToLocal(&onread) || !onread->IsFunction()) {
        return;
    }
    Local<Value> argv[2];
    if (nread > 0) {
        argv[0] = Integer::New(isolate, 0);
        argv[1] = NewBuffer(isolate, context, buf->base, static_cast<size_t>(nread));
    } else {
        argv[0] = Integer::New(isolate, static_cast<int>(nread));
        argv[1] = Undefined(isolate);
    }
    Invoke(isolate, onread.As<Function>(), object, 2, argv);
}

void ReadStart(const FunctionCallbackInfo<Value>& args) {
    TcpWrap* wrap = Unwrap(args[0]);
    if (wrap == nullptr) return args.GetReturnValue().Set(UV_EBADF);
    args.GetReturnValue().Set(uv_read_start(reinterpret_cast<uv_stream_t*>(&wrap->handle), OnAlloc, OnRead));
}

void ReadStop(const FunctionCallbackInfo<Value>& args) {
    if (TcpWrap* wrap = Unwrap(args[0])) uv_read_stop(reinterpret_cast<uv_stream_t*>(&wrap->handle));
}

// write(handle, chunk, encoding, callback): negative on error, a positive byte
// count if everything was written now (the callback isn't called), or 0 if it
// was queued, and callback(status, bytes) follows.
void Write(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    TcpWrap* wrap = Unwrap(args[0]);
    if (wrap == nullptr) return args.GetReturnValue().Set(UV_EBADF);

    // Bytes of the chunk: a Buffer's own memory, or a decoded string
    std::string decoded;
    const char* bytes;
    size_t length;
    if (args[1]->IsString()) {
        frogjs::Encoding encoding = frogjs::Encoding::kUtf8;
        String::Utf8Value name(isolate, args[2]);
        if (args[2]->IsString() && *name != nullptr && !frogjs::ParseEncoding(*name, &encoding)) {
            encoding = frogjs::Encoding::kUtf8;  // 'buffer' and such: the chunk's own encoding is UTF-8
        }
        decoded = frogjs::DecodeString(isolate, args[1].As<String>(), encoding);
        bytes = decoded.data();
        length = decoded.size();
    } else if (args[1]->IsArrayBufferView()) {
        BufferData view = GetBufferData(args[1].As<Object>());
        bytes = view.data;
        length = view.length;
    } else {
        return args.GetReturnValue().Set(UV_EINVAL);
    }
    if (length == 0) return args.GetReturnValue().Set(1.0);  // nothing to do
    if (length > frogjs::kMaxNativeCopy) return args.GetReturnValue().Set(UV_E2BIG);

    // Most writes fit in the kernel's buffer right away, without a copy or a request
    uv_stream_t* stream = reinterpret_cast<uv_stream_t*>(&wrap->handle);
    uv_buf_t attempt = uv_buf_init(const_cast<char*>(bytes), static_cast<unsigned int>(length));
    int written = uv_try_write(stream, &attempt, 1);
    if (written == UV_EAGAIN || written == UV_ENOSYS) written = 0;
    if (written < 0) return args.GetReturnValue().Set(written);
    if (static_cast<size_t>(written) == length) return args.GetReturnValue().Set(static_cast<double>(length));

    // The rest is queued: the request keeps a copy
    WriteReq* req = new WriteReq;
    req->isolate = isolate;
    req->wrap = wrap;
    req->total = length;
    req->callback.Reset(isolate, args[3].As<Function>());
    req->req.data = req;
    req->data.assign(bytes + written, length - written);

    uv_buf_t buf = uv_buf_init(&req->data[0], static_cast<unsigned int>(req->data.size()));
    int status = uv_write(&req->req, stream, &buf, 1, [](uv_write_t* r, int result) {
        WriteReq* req = static_cast<WriteReq*>(r->data);
        Isolate* isolate = req->isolate;
        if (!req->wrap->silent) {
            HandleScope handle_scope(isolate);
            Local<Value> argv[2] = {Integer::New(isolate, result), Number::New(isolate, static_cast<double>(req->total))};
            Invoke(isolate, req->callback.Get(isolate), Undefined(isolate), 2, argv);
        }
        delete req;
    });
    if (status != 0) {
        delete req;
        return args.GetReturnValue().Set(status);
    }
    args.GetReturnValue().Set(0);
}

// shutdown(handle, callback) -> status: send FIN once pending writes are done
void Shutdown(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    TcpWrap* wrap = Unwrap(args[0]);
    if (wrap == nullptr) return args.GetReturnValue().Set(UV_EBADF);

    ShutdownReq* req = new ShutdownReq;
    req->wrap = wrap;
    req->callback.Reset(isolate, args[1].As<Function>());
    req->req.data = req;
    int status = uv_shutdown(&req->req, reinterpret_cast<uv_stream_t*>(&wrap->handle), [](uv_shutdown_t* r, int result) {
        ShutdownReq* req = static_cast<ShutdownReq*>(r->data);
        Isolate* isolate = req->wrap->isolate;
        if (!req->wrap->silent) {
            HandleScope handle_scope(isolate);
            Local<Value> argv[1] = {Integer::New(isolate, result)};
            Invoke(isolate, req->callback.Get(isolate), Undefined(isolate), 1, argv);
        }
        delete req;
    });
    if (status != 0) delete req;
    args.GetReturnValue().Set(status);
}

void OnClosed(uv_handle_t* handle) {
    TcpWrap* wrap = static_cast<TcpWrap*>(handle->data);
    Isolate* isolate = wrap->isolate;
    tls_wraps.erase(wrap);
    {
        HandleScope handle_scope(isolate);
        if (!wrap->silent) {
            // Later calls with this handle find nothing
            wrap->object.Get(isolate)->SetInternalField(0, Undefined(isolate));
            if (!wrap->on_close.IsEmpty()) {
                Local<Function> callback = wrap->on_close.Get(isolate);
                Invoke(isolate, callback, Undefined(isolate), 0, nullptr);
            }
        }
    }
    delete wrap;
}

// close(handle[, callback])
void Close(const FunctionCallbackInfo<Value>& args) {
    TcpWrap* wrap = Unwrap(args[0]);
    if (wrap == nullptr) return;
    uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&wrap->handle);
    if (uv_is_closing(handle)) return;
    if (args[1]->IsFunction()) wrap->on_close.Reset(args.GetIsolate(), args[1].As<Function>());
    uv_close(handle, OnClosed);
}

// { address, family, port } from a sockaddr, or null
Local<Value> AddressObject(Isolate* isolate, Local<Context> context, const sockaddr_storage& addr) {
    char ip[INET6_ADDRSTRLEN] = {0};
    int port;
    const char* family;
    if (addr.ss_family == AF_INET6) {
        const sockaddr_in6* a = reinterpret_cast<const sockaddr_in6*>(&addr);
        uv_ip6_name(a, ip, sizeof(ip));
        port = ntohs(a->sin6_port);
        family = "IPv6";
    } else {
        const sockaddr_in* a = reinterpret_cast<const sockaddr_in*>(&addr);
        uv_ip4_name(a, ip, sizeof(ip));
        port = ntohs(a->sin_port);
        family = "IPv4";
    }
    Local<Object> result = Object::New(isolate);
    result->Set(context, String::NewFromUtf8Literal(isolate, "address"), String::NewFromUtf8(isolate, ip).ToLocalChecked()).Check();
    result->Set(context, String::NewFromUtf8Literal(isolate, "family"), String::NewFromUtf8(isolate, family).ToLocalChecked()).Check();
    result->Set(context, String::NewFromUtf8Literal(isolate, "port"), Integer::New(isolate, port)).Check();
    return result;
}

void GetSockName(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    args.GetReturnValue().SetNull();
    TcpWrap* wrap = Unwrap(args[0]);
    if (wrap == nullptr) return;
    sockaddr_storage addr;
    int length = sizeof(addr);
    if (uv_tcp_getsockname(&wrap->handle, reinterpret_cast<sockaddr*>(&addr), &length) == 0) {
        args.GetReturnValue().Set(AddressObject(isolate, isolate->GetCurrentContext(), addr));
    }
}

void GetPeerName(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    args.GetReturnValue().SetNull();
    TcpWrap* wrap = Unwrap(args[0]);
    if (wrap == nullptr) return;
    sockaddr_storage addr;
    int length = sizeof(addr);
    if (uv_tcp_getpeername(&wrap->handle, reinterpret_cast<sockaddr*>(&addr), &length) == 0) {
        args.GetReturnValue().Set(AddressObject(isolate, isolate->GetCurrentContext(), addr));
    }
}

void SetNoDelay(const FunctionCallbackInfo<Value>& args) {
    if (TcpWrap* wrap = Unwrap(args[0])) uv_tcp_nodelay(&wrap->handle, args[1]->IsTrue());
}

void SetKeepAlive(const FunctionCallbackInfo<Value>& args) {
    if (TcpWrap* wrap = Unwrap(args[0])) uv_tcp_keepalive(&wrap->handle, args[1]->IsTrue(), ToInt(args, 2));
}

void SetRef(const FunctionCallbackInfo<Value>& args) {
    if (TcpWrap* wrap = Unwrap(args[0])) {
        uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&wrap->handle);
        if (args[1]->IsTrue()) uv_ref(handle);
        else uv_unref(handle);
    }
}

// lookup(host, callback); callback(status, addresses)
void Lookup(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    std::string host = ToStdString(isolate, args[0]);

    LookupReq* req = new LookupReq;
    req->isolate = isolate;
    req->callback.Reset(isolate, args[1].As<Function>());
    req->req.data = req;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int status = uv_getaddrinfo(frogjs::Loop(), &req->req, [](uv_getaddrinfo_t* r, int result, struct addrinfo* res) {
        LookupReq* req = static_cast<LookupReq*>(r->data);
        Isolate* isolate = req->isolate;
        {
            HandleScope handle_scope(isolate);
            Local<Context> context = isolate->GetCurrentContext();
            Local<Array> addresses = Array::New(isolate);
            uint32_t count = 0;
            for (struct addrinfo* ai = res; ai != nullptr && result == 0; ai = ai->ai_next) {
                char ip[INET6_ADDRSTRLEN] = {0};
                if (ai->ai_family == AF_INET) {
                    uv_ip4_name(reinterpret_cast<sockaddr_in*>(ai->ai_addr), ip, sizeof(ip));
                } else if (ai->ai_family == AF_INET6) {
                    uv_ip6_name(reinterpret_cast<sockaddr_in6*>(ai->ai_addr), ip, sizeof(ip));
                } else {
                    continue;
                }
                addresses->Set(context, count++, String::NewFromUtf8(isolate, ip).ToLocalChecked()).Check();
            }
            int status = result;
            if (status == 0 && count == 0) status = UV_EAI_NONAME;
            Local<Value> argv[2] = {Integer::New(isolate, status), addresses};
            Invoke(isolate, req->callback.Get(isolate), Undefined(isolate), 2, argv);
        }
        uv_freeaddrinfo(res);
        delete req;
    }, host.c_str(), nullptr, &hints);
    if (status != 0) {
        // Report it the same way a failed lookup is reported
        Local<Value> argv[2] = {Integer::New(isolate, status), Array::New(isolate)};
        delete req;
        (void)args[1].As<Function>()->Call(isolate->GetCurrentContext(), Undefined(isolate), 2, argv);  // an exception propagates
    }
}

void ErrName(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(String::NewFromUtf8(args.GetIsolate(), uv_err_name(ToInt(args, 0))).ToLocalChecked());
}

void ParseIPv6(const FunctionCallbackInfo<Value>& args) {
    sockaddr_in6 addr;
    args.GetReturnValue().Set(uv_ip6_addr(ToStdString(args.GetIsolate(), args[0]).c_str(), 0, &addr) == 0);
}

void CleanupNet() {
    for (TcpWrap* wrap : tls_wraps) {
        wrap->silent = true;
        uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&wrap->handle);
        if (!uv_is_closing(handle)) uv_close(handle, OnClosed);
    }
    tls_tcp_template.Reset();
    tls_cleanup_registered = false;
}

}  // namespace

// Set up the net module (requires SetupStream first)
void SetupNet(Isolate* isolate, Local<Context> context) {
    Local<FunctionTemplate> tcp_template = FunctionTemplate::New(isolate);
    tcp_template->InstanceTemplate()->SetInternalFieldCount(1);
    tls_tcp_template.Reset(isolate, tcp_template);

    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "newTcp", NewTcp);
    frogjs::SetFunction(isolate, context, native, "listen", Listen);
    frogjs::SetFunction(isolate, context, native, "connect", Connect);
    frogjs::SetFunction(isolate, context, native, "readStart", ReadStart);
    frogjs::SetFunction(isolate, context, native, "readStop", ReadStop);
    frogjs::SetFunction(isolate, context, native, "write", Write);
    frogjs::SetFunction(isolate, context, native, "shutdown", Shutdown);
    frogjs::SetFunction(isolate, context, native, "close", Close);
    frogjs::SetFunction(isolate, context, native, "getsockname", GetSockName);
    frogjs::SetFunction(isolate, context, native, "getpeername", GetPeerName);
    frogjs::SetFunction(isolate, context, native, "setNoDelay", SetNoDelay);
    frogjs::SetFunction(isolate, context, native, "setKeepAlive", SetKeepAlive);
    frogjs::SetFunction(isolate, context, native, "setRef", SetRef);
    frogjs::SetFunction(isolate, context, native, "lookup", Lookup);
    frogjs::SetFunction(isolate, context, native, "errname", ErrName);
    frogjs::SetFunction(isolate, context, native, "parseIPv6", ParseIPv6);
    native->Set(context, String::NewFromUtf8Literal(isolate, "UV_EOF"), Integer::New(isolate, UV_EOF)).Check();
    native->Set(context, String::NewFromUtf8Literal(isolate, "UV_ECANCELED"), Integer::New(isolate, UV_ECANCELED)).Check();

    Local<Object> global = context->Global();
    auto get = [&](Local<String> name) { return global->Get(context, name).ToLocalChecked(); };
    Local<Value> argv[3] = {
        native,
        get(String::NewFromUtf8Literal(isolate, "stream")),
        get(String::NewFromUtf8Literal(isolate, "events")),
    };
    Local<Value> net;
    if (!frogjs::RunJsModule(isolate, context, "net", kNetSource, 3, argv).ToLocal(&net)) return;
    global->Set(context, String::NewFromUtf8Literal(isolate, "net"), net).Check();

    if (!tls_cleanup_registered) {
        tls_cleanup_registered = true;
        frogjs::AddThreadCleanup(CleanupNet);
    }
}
