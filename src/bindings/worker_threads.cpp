#include <v8.h>
#include <uv.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "frogjs/buffer.h"
#include "frogjs/errors.h"
#include "frogjs/js_module.h"
#include "frogjs/loop.h"
#include "frogjs/runtime.h"
#include "frogjs/util.h"
#include "frogjs/v8_compat.h"
#include "frogjs/worker.h"

using namespace v8;

// worker_threads: each Worker runs its script in a new isolate with its own
// event loop on its own thread. Threads share nothing but messages: values are
// copied with V8's structured clone (ValueSerializer); SharedArrayBuffers are
// shared, and ArrayBuffers and MessagePorts in a transfer list are moved.
static const char* kWorkerThreadsSource = R"JS(
(function (native, EventEmitter, path) {
'use strict';

const { MessagePort } = native;
const kPort = Symbol('kPort');
const kHandle = Symbol('kHandle');
const kOnMessage = Symbol('kOnMessage');
const kExitPromises = Symbol('kExitPromises');

function makeError(Ctor, code, message) {
  const err = new Ctor(message);
  err.code = code;
  return err;
}

function describe(value) {
  if (value === null || value === undefined) return String(value);
  if (typeof value === 'function') return 'function ' + (value.name || '<anonymous>');
  if (typeof value === 'object') return 'an instance of ' + ((value.constructor && value.constructor.name) || 'Object');
  let shown = typeof value === 'string' ? `'${value}'` : String(value);
  if (shown.length > 28) shown = shown.slice(0, 25) + '...';
  return `type ${typeof value} (${shown})`;
}

// postMessage(value, transferList) or postMessage(value, { transfer })
function transferListOf(transfer) {
  if (transfer === undefined || transfer === null) return [];
  if (Array.isArray(transfer)) return transfer;
  if (typeof transfer === 'object' && transfer.transfer !== undefined) {
    if (!Array.isArray(transfer.transfer)) {
      throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
        'The "options.transfer" property must be of type Array. Received ' + describe(transfer.transfer));
    }
    return transfer.transfer;
  }
  if (typeof transfer === 'object') return [];
  throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
    'Optional transferList argument must be an iterable');
}

// MessagePort: an EventEmitter whose instances are created natively (so they
// can be transferred). Adding a 'message' listener starts delivery.
Object.setPrototypeOf(MessagePort.prototype, EventEmitter.prototype);
Object.setPrototypeOf(MessagePort, EventEmitter);

function updateDelivery(port, type) {
  if (type !== 'message') return;
  if (port.listenerCount('message') > 0) native.start(port);
  else native.stop(port);
}

for (const name of ['on', 'addListener', 'once', 'prependListener', 'prependOnceListener']) {
  const method = EventEmitter.prototype[name];
  MessagePort.prototype[name] = function (type, listener) {
    method.call(this, type, listener);
    updateDelivery(this, type);
    return this;
  };
}
for (const name of ['off', 'removeListener']) {
  const method = EventEmitter.prototype[name];
  MessagePort.prototype[name] = function (type, listener) {
    method.call(this, type, listener);
    updateDelivery(this, type);
    return this;
  };
}
MessagePort.prototype.removeAllListeners = function (type) {
  EventEmitter.prototype.removeAllListeners.apply(this, arguments);
  updateDelivery(this, 'message');
  return this;
};

Object.assign(MessagePort.prototype, {
  postMessage(value, transfer) {
    native.postMessage(this, value, transferListOf(transfer));
  },
  start() { native.start(this); },
  close() { native.close(this); },
  ref() { native.ref(this, true); return this; },
  unref() { native.ref(this, false); return this; },
  hasRef() { return native.hasRef(this); },
});

// port.onmessage = (event) => event.data, as in the web API
Object.defineProperty(MessagePort.prototype, 'onmessage', {
  configurable: true,
  enumerable: true,
  get() { return this[kOnMessage] ? this[kOnMessage].handler : null; },
  set(handler) {
    if (this[kOnMessage]) this.removeListener('message', this[kOnMessage]);
    this[kOnMessage] = undefined;
    if (typeof handler === 'function') {
      const listener = (data) => handler.call(this, { type: 'message', data, target: this });
      listener.handler = handler;
      this[kOnMessage] = listener;
      this.on('message', listener);
    }
  },
});

Object.defineProperty(MessagePort.prototype, Symbol.toStringTag, { value: 'MessagePort', configurable: true });

native.setPortEmitter((port, type, value) => {
  if (type === 'message') port.emit('message', value);
  else port.emit('close');
});

class MessageChannel {
  constructor() {
    const [port1, port2] = native.createChannel();
    this.port1 = port1;
    this.port2 = port2;
  }
}

function receiveMessageOnPort(port) {
  if (!(port instanceof MessagePort)) {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
      'The "port" argument must be a MessagePort instance');
  }
  return native.receiveMessage(port);
}

function workerPath(filename, options) {
  if (filename && typeof filename === 'object' && typeof filename.href === 'string') {
    if (filename.protocol !== 'file:') {
      throw makeError(TypeError, 'ERR_INVALID_URL_SCHEME', 'The URL must be of scheme file');
    }
    return decodeURIComponent(filename.pathname);
  }
  if (typeof filename !== 'string') {
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
      'The "filename" argument must be of type string or an instance of URL. Received ' + describe(filename));
  }
  if (options.eval) return filename;
  if (filename.startsWith('file://')) return decodeURIComponent(filename.slice(7));
  if (!path.isAbsolute(filename) && !/^\.\.?[\\/]/.test(filename)) {
    throw makeError(TypeError, 'ERR_WORKER_PATH',
      'The worker script or module filename must be an absolute path or a relative path ' +
      `starting with './' or '../'. Received "${filename}"`);
  }
  return path.resolve(filename);
}

class Worker extends EventEmitter {
  constructor(filename, options = {}) {
    super();
    if (options === null || typeof options !== 'object') {
      throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
        'The "options" argument must be of type object. Received ' + describe(options));
    }
    const script = workerPath(filename, options);
    let argv = [];
    if (options.argv !== undefined) {
      if (!Array.isArray(options.argv)) {
        throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
          'The "options.argv" property must be an instance of Array. Received ' + describe(options.argv));
      }
      argv = options.argv.map(String);
    }

    const { handle, port, threadId } = native.createWorker(
      script, !!options.eval, argv, options.workerData, transferListOf(options.transferList));
    handle.worker = this;
    this[kHandle] = handle;
    this[kPort] = port;
    this[kExitPromises] = [];
    this.threadId = threadId;
    this.exitCode = undefined;

    // The worker handle keeps the process alive, not its port
    port.on('message', (value) => this.emit('message', value));
    port.unref();
  }

  postMessage(value, transfer) {
    if (this.exitCode !== undefined) return;
    this[kPort].postMessage(value, transfer);
  }

  terminate(callback) {
    if (typeof callback === 'function') {
      process.emitWarning?.('Passing a callback to worker.terminate() is deprecated.');
      this.once('exit', (code) => callback(null, code));
    }
    if (this.exitCode !== undefined) return Promise.resolve(undefined);
    return new Promise((resolve) => {
      this[kExitPromises].push(resolve);
      native.terminate(this[kHandle]);
    });
  }

  ref() { if (this.exitCode === undefined) native.workerRef(this[kHandle], true); }
  unref() { if (this.exitCode === undefined) native.workerRef(this[kHandle], false); }
}

native.setWorkerEmitter((handle, type, value) => {
  const worker = handle.worker;
  if (type === 'online') {
    worker.emit('online');
  } else if (type === 'error') {
    const [error, extra] = value;
    if (error !== null && typeof error === 'object') Object.assign(error, extra);
    worker.emit('error', error);
  } else if (type === 'exit') {
    // Messages the worker posted before it ended arrive before 'exit'
    native.drain(worker[kPort]);
    worker[kPort].close();
    worker.exitCode = value;
    worker.threadId = -1;
    for (const resolve of worker[kExitPromises].splice(0)) resolve(value);
    worker.emit('exit', value);
  }
});

let parentPort = null;
if (!native.isMainThread) {
  parentPort = native.parentPort;
}

return {
  isMainThread: native.isMainThread,
  threadId: native.threadId,
  parentPort,
  workerData: native.workerData,
  resourceLimits: {},
  Worker,
  MessageChannel,
  MessagePort,
  receiveMessageOnPort,
};
})
)JS";

namespace {

struct PortHandle;

// A serialized message: V8 serializer output plus the memory it refers to
struct Message {
    std::vector<uint8_t> data;
    std::vector<std::shared_ptr<BackingStore>> shared_buffers;       // SharedArrayBuffers
    std::vector<std::shared_ptr<BackingStore>> transferred_buffers;  // moved ArrayBuffers
    std::vector<std::shared_ptr<struct PortEndpoint>> ports;         // moved MessagePorts
};

// One end of a message channel. It outlives the thread that uses it while it
// is being transferred, so its JS side (handle) is attached and detached.
struct PortEndpoint {
    std::deque<std::unique_ptr<Message>> incoming;
    std::weak_ptr<PortEndpoint> peer;
    PortHandle* handle = nullptr;  // the owning thread's MessagePort; null in transit
    bool closed = false;           // this end or its peer was closed
};

// Guards every PortEndpoint (messages are rare enough that one lock will do)
std::mutex g_port_mutex;

// The MessagePort object of an endpoint, on the thread that owns the endpoint
struct PortHandle {
    Isolate* isolate;
    Global<Context> context;
    Global<Object> object;
    uv_async_t async;
    std::shared_ptr<PortEndpoint> endpoint;
    bool started = false;  // delivering 'message' events
    bool refed = true;     // ref() / unref()
    bool closing = false;
    bool emit_close = false;  // emit 'close' once the handle is closed
};

// Per-thread state (Persistent: no Reset when the thread exits, after V8)
thread_local Persistent<FunctionTemplate> g_port_template;
thread_local Persistent<Function> g_port_emitter;
thread_local Persistent<Function> g_worker_emitter;
thread_local std::vector<PortHandle*> g_thread_ports;

const int kHostObjectPort = 0;
const int kHostObjectBuffer = 1;

Local<String> V8String(Isolate* isolate, const std::string& value) {
    return String::NewFromUtf8(isolate, value.c_str(), NewStringType::kNormal,
        static_cast<int>(value.size())).ToLocalChecked();
}

void ThrowTypeError(Isolate* isolate, const char* code, const std::string& message) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> error = Exception::TypeError(V8String(isolate, message)).As<Object>();
    error->Set(context, String::NewFromUtf8Literal(isolate, "code"), String::NewFromUtf8(isolate, code).ToLocalChecked()).Check();
    isolate->ThrowException(error);
}

// The DOMException Node.js throws for values that can't be cloned
Local<Value> NewDataCloneError(Isolate* isolate, Local<String> message) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> error = Exception::Error(message).As<Object>();
    error->Set(context, String::NewFromUtf8Literal(isolate, "name"), String::NewFromUtf8Literal(isolate, "DataCloneError")).Check();
    error->Set(context, String::NewFromUtf8Literal(isolate, "code"), Integer::New(isolate, 25)).Check();
    return error;
}

PortHandle* UnwrapPort(Local<Value> value) {
    if (!value->IsObject() || g_port_template.IsEmpty()) return nullptr;
    Local<Object> object = value.As<Object>();
    Isolate* isolate = object->GetIsolate();
    if (!g_port_template.Get(isolate)->HasInstance(object) || object->InternalFieldCount() < 1) return nullptr;
    Local<Value> field = object->GetInternalField(0).As<Value>();
    if (!field->IsExternal()) return nullptr;
    return frogjs::ExternalValue<PortHandle>(field);
}

void UpdatePortRef(PortHandle* handle) {
    uv_handle_t* async = reinterpret_cast<uv_handle_t*>(&handle->async);
    if (handle->started && handle->refed && !handle->closing) uv_ref(async);
    else uv_unref(async);
}

void CallEmitter(Isolate* isolate, Local<Context> context, Persistent<Function>& emitter, int argc, Local<Value> argv[]) {
    if (emitter.IsEmpty()) return;
    TryCatch try_catch(isolate);
    Local<Value> ignored;
    if (!emitter.Get(isolate)->Call(context, Undefined(isolate), argc, argv).ToLocal(&ignored) &&
        try_catch.HasCaught()) {
        frogjs::ReportException(isolate, &try_catch);
    }
}

void OnPortAsync(uv_async_t* async);
Local<Object> NewPort(Isolate* isolate, Local<Context> context, std::shared_ptr<PortEndpoint> endpoint);
void ClosePort(PortHandle* handle, bool emit_close);

// ---- Serialization ------------------------------------------------------

class Serializer : public ValueSerializer::Delegate {
 public:
    Serializer(Isolate* isolate, Message* message) : isolate_(isolate), message_(message), serializer_(isolate, this) {}

    // Serialize value into the message. Transferred ArrayBuffers are detached
    // and transferred ports leave this thread, but only once nothing can fail.
    bool Write(Local<Context> context, Local<Value> value, Local<Array> transfer_list) {
        std::vector<Local<ArrayBuffer>> buffers;
        for (uint32_t i = 0; i < transfer_list->Length(); i++) {
            Local<Value> item;
            if (!transfer_list->Get(context, i).ToLocal(&item)) return false;
            if (item->IsArrayBuffer()) {
                Local<ArrayBuffer> buffer = item.As<ArrayBuffer>();
                if (std::find(buffers.begin(), buffers.end(), buffer) != buffers.end()) {
                    return ThrowCloneError("ArrayBuffer at index " + std::to_string(i) + " is a duplicate of an earlier ArrayBuffer");
                }
                if (buffer->WasDetached() || !buffer->IsDetachable()) {
                    return ThrowCloneError("An ArrayBuffer is detached and could not be cloned.");
                }
                serializer_.TransferArrayBuffer(static_cast<uint32_t>(buffers.size()), buffer);
                buffers.push_back(buffer);
            } else if (PortHandle* port = UnwrapPort(item)) {
                if (port->closing || port->endpoint == nullptr) {
                    return ThrowCloneError("MessagePort in transfer list is already detached");
                }
                if (std::find(ports_.begin(), ports_.end(), port) != ports_.end()) {
                    return ThrowCloneError("Transfer list contains duplicate MessagePort");
                }
                ports_.push_back(port);
                port_objects_.push_back(item.As<Object>());
            } else {
                ThrowTypeError(isolate_, "ERR_INVALID_TRANSFER_OBJECT", "Found invalid value in transferList.");
                return false;
            }
        }

        serializer_.WriteHeader();
        if (!serializer_.WriteValue(context, value).FromMaybe(false)) return false;

        std::pair<uint8_t*, size_t> data = serializer_.Release();
        message_->data.assign(data.first, data.first + data.second);
        free(data.first);

        for (Local<ArrayBuffer> buffer : buffers) {
            message_->transferred_buffers.push_back(buffer->GetBackingStore());
            buffer->Detach(Local<Value>()).Check();
        }
        for (PortHandle* port : ports_) {
            message_->ports.push_back(port->endpoint);
            DetachPort(port);
        }
        return true;
    }

    // A port that is being sent away stops receiving on this thread
    static void DetachPort(PortHandle* port) {
        {
            std::lock_guard<std::mutex> lock(g_port_mutex);
            port->endpoint->handle = nullptr;
        }
        port->endpoint.reset();
        ClosePort(port, false);
    }

    void ThrowDataCloneError(Local<String> message) override {
        isolate_->ThrowException(NewDataCloneError(isolate_, message));
    }

    Maybe<bool> WriteHostObject(Isolate* isolate, Local<Object> object) override {
        for (size_t i = 0; i < port_objects_.size(); i++) {
            if (port_objects_[i] == object) {
                serializer_.WriteUint32(kHostObjectPort);
                serializer_.WriteUint32(static_cast<uint32_t>(i));
                return Just(true);
            }
        }
        if (UnwrapPort(object) != nullptr) {
            ThrowCloneError("Object that needs transfer was found in message but not listed in transferList");
            return Nothing<bool>();
        }
        if (IsBuffer(object)) {
            BufferData* buffer = GetBufferData(object);
            serializer_.WriteUint32(kHostObjectBuffer);
            serializer_.WriteUint64(buffer->length);
            serializer_.WriteRawBytes(buffer->data, buffer->length);
            return Just(true);
        }
        Local<String> name = object->GetConstructorName();
        String::Utf8Value utf8(isolate, name);
        ThrowCloneError(std::string(*utf8 ? *utf8 : "Object") + " object could not be cloned.");
        return Nothing<bool>();
    }

    Maybe<uint32_t> GetSharedArrayBufferId(Isolate*, Local<SharedArrayBuffer> buffer) override {
        for (size_t i = 0; i < shared_.size(); i++) {
            if (shared_[i] == buffer) return Just(static_cast<uint32_t>(i));
        }
        shared_.push_back(buffer);
        message_->shared_buffers.push_back(buffer->GetBackingStore());
        return Just(static_cast<uint32_t>(shared_.size() - 1));
    }

 private:
    bool ThrowCloneError(const std::string& message) {
        isolate_->ThrowException(NewDataCloneError(isolate_, V8String(isolate_, message)));
        return false;
    }

    Isolate* isolate_;
    Message* message_;
    ValueSerializer serializer_;
    std::vector<PortHandle*> ports_;
    std::vector<Local<Object>> port_objects_;
    std::vector<Local<SharedArrayBuffer>> shared_;
};

class Deserializer : public ValueDeserializer::Delegate {
 public:
    Deserializer(Isolate* isolate, Message* message)
        : isolate_(isolate), message_(message),
          deserializer_(isolate, message->data.data(), message->data.size(), this) {}

    MaybeLocal<Value> Read(Local<Context> context) {
        EscapableHandleScope handle_scope(isolate_);
        for (size_t i = 0; i < message_->transferred_buffers.size(); i++) {
            deserializer_.TransferArrayBuffer(static_cast<uint32_t>(i),
                ArrayBuffer::New(isolate_, message_->transferred_buffers[i]));
        }
        // Ports are attached to this thread up front, so none is left in limbo
        for (auto& endpoint : message_->ports) {
            ports_.push_back(NewPort(isolate_, context, endpoint));
        }
        if (!deserializer_.ReadHeader(context).FromMaybe(false)) return MaybeLocal<Value>();
        Local<Value> value;
        if (!deserializer_.ReadValue(context).ToLocal(&value)) return MaybeLocal<Value>();
        return handle_scope.Escape(value);
    }

    MaybeLocal<Object> ReadHostObject(Isolate* isolate) override {
        uint32_t kind = 0;
        if (!deserializer_.ReadUint32(&kind)) return MaybeLocal<Object>();
        if (kind == kHostObjectPort) {
            uint32_t index = 0;
            if (!deserializer_.ReadUint32(&index) || index >= ports_.size()) return MaybeLocal<Object>();
            return ports_[index];
        }
        if (kind == kHostObjectBuffer) {
            uint64_t length = 0;
            const void* data = nullptr;
            if (!deserializer_.ReadUint64(&length) || !deserializer_.ReadRawBytes(length, &data)) {
                return MaybeLocal<Object>();
            }
            // A Buffer arrives as a Uint8Array, as in Node.js
            std::unique_ptr<BackingStore> store = ArrayBuffer::NewBackingStore(isolate, length);
            if (length > 0) memcpy(store->Data(), data, length);
            Local<ArrayBuffer> buffer = ArrayBuffer::New(isolate, std::move(store));
            return Uint8Array::New(buffer, 0, length);
        }
        isolate->ThrowException(Exception::Error(String::NewFromUtf8Literal(isolate, "Unable to deserialize cloned data.")));
        return MaybeLocal<Object>();
    }

    MaybeLocal<SharedArrayBuffer> GetSharedArrayBufferFromId(Isolate* isolate, uint32_t id) override {
        if (id >= message_->shared_buffers.size()) return MaybeLocal<SharedArrayBuffer>();
        return SharedArrayBuffer::New(isolate, message_->shared_buffers[id]);
    }

 private:
    Isolate* isolate_;
    Message* message_;
    ValueDeserializer deserializer_;
    std::vector<Local<Object>> ports_;
};

std::unique_ptr<Message> Serialize(Isolate* isolate, Local<Context> context, Local<Value> value,
                                   Local<Value> transfer_list, PortHandle* source = nullptr) {
    auto message = std::make_unique<Message>();
    Serializer serializer(isolate, message.get());
    Local<Array> transfer = transfer_list->IsArray() ? transfer_list.As<Array>() : Array::New(isolate);
    if (source != nullptr) {
        // Checked before anything is detached
        for (uint32_t i = 0; i < transfer->Length(); i++) {
            Local<Value> item;
            if (transfer->Get(context, i).ToLocal(&item) && UnwrapPort(item) == source) {
                isolate->ThrowException(NewDataCloneError(isolate,
                    String::NewFromUtf8Literal(isolate, "Transfer list contains source port")));
                return nullptr;
            }
        }
    }
    if (!serializer.Write(context, value, transfer)) return nullptr;
    return message;
}

// ---- MessagePort ----------------------------------------------------------

std::pair<std::shared_ptr<PortEndpoint>, std::shared_ptr<PortEndpoint>> NewChannel() {
    auto a = std::make_shared<PortEndpoint>();
    auto b = std::make_shared<PortEndpoint>();
    a->peer = b;
    b->peer = a;
    return {a, b};
}

void OnPortCollected(const WeakCallbackInfo<PortHandle>& info) {
    PortHandle* handle = info.GetParameter();
    handle->object.Reset();
    ClosePort(handle, false);
}

// Ports nobody listens to may be garbage collected; listening ones may not
void UpdatePortWeakness(PortHandle* handle) {
    if (handle->object.IsEmpty()) return;
    if (handle->started && !handle->closing) handle->object.ClearWeak();
    else handle->object.SetWeak(handle, OnPortCollected, WeakCallbackType::kParameter);
}

Local<Object> NewPort(Isolate* isolate, Local<Context> context, std::shared_ptr<PortEndpoint> endpoint) {
    EscapableHandleScope handle_scope(isolate);
    Local<Object> object = g_port_template.Get(isolate)->InstanceTemplate()->NewInstance(context).ToLocalChecked();

    PortHandle* handle = new PortHandle;
    handle->isolate = isolate;
    handle->context.Reset(isolate, context);
    handle->object.Reset(isolate, object);
    handle->endpoint = endpoint;
    handle->async.data = handle;
    uv_async_init(frogjs::Loop(), &handle->async, OnPortAsync);
    UpdatePortRef(handle);
    UpdatePortWeakness(handle);
    object->SetInternalField(0, frogjs::NewExternal(isolate, handle));
    g_thread_ports.push_back(handle);

    std::lock_guard<std::mutex> lock(g_port_mutex);
    endpoint->handle = handle;
    if (!endpoint->incoming.empty() || endpoint->closed) uv_async_send(&handle->async);
    return handle_scope.Escape(object);
}

// Close a port: the peer is told (and closes too). emit_close emits 'close'
// on this port once its handle is closed.
void ClosePort(PortHandle* handle, bool emit_close) {
    if (handle->closing) return;
    handle->closing = true;
    if (handle->endpoint != nullptr) {
        std::lock_guard<std::mutex> lock(g_port_mutex);
        handle->endpoint->closed = true;
        handle->endpoint->handle = nullptr;
        handle->endpoint->incoming.clear();
        std::shared_ptr<PortEndpoint> peer = handle->endpoint->peer.lock();
        if (peer != nullptr && !peer->closed) {
            peer->closed = true;
            if (peer->handle != nullptr) uv_async_send(&peer->handle->async);
        }
    }
    g_thread_ports.erase(std::remove(g_thread_ports.begin(), g_thread_ports.end(), handle), g_thread_ports.end());

    if (!handle->object.IsEmpty()) {
        HandleScope handle_scope(handle->isolate);
        handle->object.Get(handle->isolate)->SetInternalField(0, Undefined(handle->isolate));
        // Kept alive until 'close' has been emitted
        handle->object.ClearWeak();
    }
    handle->emit_close = emit_close;
    uv_close(reinterpret_cast<uv_handle_t*>(&handle->async), [](uv_handle_t* async) {
        PortHandle* handle = static_cast<PortHandle*>(async->data);
        if (handle->emit_close && !handle->object.IsEmpty() && !handle->isolate->IsExecutionTerminating()) {
            Isolate* isolate = handle->isolate;
            HandleScope handle_scope(isolate);
            Local<Context> context = handle->context.Get(isolate);
            Context::Scope context_scope(context);
            Local<Value> argv[2] = {handle->object.Get(isolate), String::NewFromUtf8Literal(isolate, "close")};
            CallEmitter(isolate, context, g_port_emitter, 2, argv);
        }
        delete handle;
    });
}

// Take the next message for a port, if any
std::unique_ptr<Message> NextMessage(PortHandle* handle) {
    std::lock_guard<std::mutex> lock(g_port_mutex);
    if (handle->endpoint == nullptr || handle->endpoint->incoming.empty()) return nullptr;
    std::unique_ptr<Message> message = std::move(handle->endpoint->incoming.front());
    handle->endpoint->incoming.pop_front();
    return message;
}

MaybeLocal<Value> ReadMessage(Isolate* isolate, Local<Context> context, Message* message) {
    Deserializer deserializer(isolate, message);
    return deserializer.Read(context);
}

// Deliver queued messages while the port is started, then close it if its
// peer has gone away
void DeliverMessages(PortHandle* handle) {
    Isolate* isolate = handle->isolate;
    HandleScope handle_scope(isolate);
    Local<Context> context = handle->context.Get(isolate);
    Context::Scope context_scope(context);

    while (handle->started && !handle->closing && !isolate->IsExecutionTerminating()) {
        std::unique_ptr<Message> message = NextMessage(handle);
        if (message == nullptr) break;
        HandleScope message_scope(isolate);
        TryCatch try_catch(isolate);
        Local<Value> value;
        if (!ReadMessage(isolate, context, message.get()).ToLocal(&value)) {
            if (try_catch.HasCaught()) frogjs::ReportException(isolate, &try_catch);
            continue;
        }
        Local<Value> argv[3] = {handle->object.Get(isolate), String::NewFromUtf8Literal(isolate, "message"), value};
        CallEmitter(isolate, context, g_port_emitter, 3, argv);
    }

    if (handle->closing) return;
    bool peer_closed;
    {
        std::lock_guard<std::mutex> lock(g_port_mutex);
        peer_closed = handle->endpoint != nullptr && handle->endpoint->closed &&
                      (!handle->started || handle->endpoint->incoming.empty());
    }
    if (peer_closed) ClosePort(handle, true);
}

void OnPortAsync(uv_async_t* async) {
    PortHandle* handle = static_cast<PortHandle*>(async->data);
    if (handle->closing) return;
    DeliverMessages(handle);
}

// MessagePort constructor: only FrogJS creates ports
void PortConstructor(const FunctionCallbackInfo<Value>& args) {
    ThrowTypeError(args.GetIsolate(), "ERR_CONSTRUCT_CALL_INVALID", "Constructor cannot be called");
}

PortHandle* PortArg(const FunctionCallbackInfo<Value>& args) {
    PortHandle* handle = UnwrapPort(args[0]);
    return handle != nullptr && !handle->closing ? handle : nullptr;
}

void CreateChannel(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    auto [a, b] = NewChannel();
    Local<Value> ports[2] = {NewPort(isolate, context, a), NewPort(isolate, context, b)};
    args.GetReturnValue().Set(Array::New(isolate, ports, 2));
}

// postMessage(port, value, transferList)
void PostMessage(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    PortHandle* handle = UnwrapPort(args[0]);
    if (handle == nullptr) {
        ThrowTypeError(isolate, "ERR_INVALID_THIS", "Value of \"this\" must be of type MessagePort");
        return;
    }
    // Like Node.js the value is still cloned (and can throw) on a closed port
    std::unique_ptr<Message> message = Serialize(isolate, context, args[1], args[2], handle);
    if (message == nullptr || handle->closing || handle->endpoint == nullptr) return;

    std::lock_guard<std::mutex> lock(g_port_mutex);
    std::shared_ptr<PortEndpoint> peer = handle->endpoint->peer.lock();
    if (handle->endpoint->closed || peer == nullptr || peer->closed) return;
    peer->incoming.push_back(std::move(message));
    if (peer->handle != nullptr) uv_async_send(&peer->handle->async);
}

void Start(const FunctionCallbackInfo<Value>& args) {
    PortHandle* handle = PortArg(args);
    if (handle == nullptr || handle->started) return;
    handle->started = true;
    UpdatePortRef(handle);
    UpdatePortWeakness(handle);
    uv_async_send(&handle->async);
}

void Stop(const FunctionCallbackInfo<Value>& args) {
    PortHandle* handle = PortArg(args);
    if (handle == nullptr || !handle->started) return;
    handle->started = false;
    UpdatePortRef(handle);
    UpdatePortWeakness(handle);
}

void Ref(const FunctionCallbackInfo<Value>& args) {
    PortHandle* handle = PortArg(args);
    if (handle == nullptr) return;
    handle->refed = args[1]->IsTrue();
    UpdatePortRef(handle);
}

void HasRef(const FunctionCallbackInfo<Value>& args) {
    PortHandle* handle = PortArg(args);
    args.GetReturnValue().Set(handle != nullptr && uv_has_ref(reinterpret_cast<uv_handle_t*>(&handle->async)) != 0);
}

void Close(const FunctionCallbackInfo<Value>& args) {
    PortHandle* handle = PortArg(args);
    if (handle != nullptr) ClosePort(handle, true);
}

// drain(port): deliver whatever is queued now (before a Worker's 'exit')
void Drain(const FunctionCallbackInfo<Value>& args) {
    PortHandle* handle = PortArg(args);
    if (handle != nullptr) DeliverMessages(handle);
}

// receiveMessage(port): { message } for the next queued message, or undefined
void ReceiveMessage(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    PortHandle* handle = PortArg(args);
    if (handle == nullptr) return;
    std::unique_ptr<Message> message = NextMessage(handle);
    if (message == nullptr) return;
    Local<Value> value;
    if (!ReadMessage(isolate, context, message.get()).ToLocal(&value)) return;
    Local<Object> result = Object::New(isolate);
    result->Set(context, String::NewFromUtf8Literal(isolate, "message"), value).Check();
    args.GetReturnValue().Set(result);
}

void SetPortEmitter(const FunctionCallbackInfo<Value>& args) {
    if (args[0]->IsFunction()) g_port_emitter.Reset(args.GetIsolate(), args[0].As<Function>());
}

void SetWorkerEmitter(const FunctionCallbackInfo<Value>& args) {
    if (args[0]->IsFunction()) g_worker_emitter.Reset(args.GetIsolate(), args[0].As<Function>());
}

// ---- Worker ---------------------------------------------------------------

struct WorkerHandle;

// State shared by a worker thread and the thread that created it
struct WorkerThread {
    int thread_id = 0;
    std::string script;  // filename, or the code with eval
    bool is_eval = false;
    std::vector<std::string> argv;
    std::unique_ptr<Message> worker_data;
    std::shared_ptr<PortEndpoint> parent_port;  // the worker's end of the channel
    uv_loop_t* parent_loop = nullptr;
    WorkerHandle* parent = nullptr;  // owned by the parent thread
    uv_thread_t thread;
    bool joined = false;             // parent thread only

    std::mutex mutex;  // guards the fields below
    Isolate* isolate = nullptr;      // while the worker runs JavaScript
    uv_async_t* stop_async = nullptr;
    std::atomic<bool> stopping{false};
    bool exit_code_set = false;
    int exit_code = 0;
    bool online = false;
    bool exited = false;
    std::unique_ptr<Message> error;  // uncaught error, serialized
};

// The parent thread's side: the JS handle and an async the worker signals
struct WorkerHandle {
    Isolate* isolate;
    Global<Context> context;
    Global<Object> object;
    uv_async_t async;
    std::shared_ptr<WorkerThread> worker;
    bool online_emitted = false;
    bool error_emitted = false;
};

std::atomic<int> g_next_thread_id{1};
std::mutex g_workers_mutex;
std::vector<std::shared_ptr<WorkerThread>> g_workers;  // not yet joined

// The worker running on this thread (null on the main thread)
thread_local WorkerThread* tls_worker = nullptr;

void SignalParent(WorkerThread* worker) {
    // The parent's handle lives until the worker has exited and been joined
    uv_async_send(&worker->parent->async);
}

// Record how the worker ends (first reason wins) and stop it
void StopWorker(WorkerThread* worker, int exit_code) {
    std::lock_guard<std::mutex> lock(worker->mutex);
    if (!worker->exit_code_set) {
        worker->exit_code_set = true;
        worker->exit_code = exit_code;
    }
    worker->stopping = true;
    if (worker->isolate != nullptr) {
        worker->isolate->TerminateExecution();
        uv_async_send(worker->stop_async);
    }
}

void JoinWorker(WorkerThread* worker) {
    if (worker->joined) return;
    uv_thread_join(&worker->thread);
    worker->joined = true;
    std::lock_guard<std::mutex> lock(g_workers_mutex);
    g_workers.erase(std::remove_if(g_workers.begin(), g_workers.end(),
        [worker](const std::shared_ptr<WorkerThread>& w) { return w.get() == worker; }), g_workers.end());
}

// Store an uncaught error for the parent's 'error' event. It is sent as
// [error, extra properties]: the structured clone of an Error keeps only its
// name, message and stack, not properties such as err.code.
void SetWorkerError(Isolate* isolate, Local<Context> context, WorkerThread* worker, Local<Value> exception) {
    std::unique_ptr<Message> message;
    {
        TryCatch try_catch(isolate);
        Local<Object> extra = Object::New(isolate);
        if (exception->IsNativeError()) {
            Local<Object> error = exception.As<Object>();
            Local<Array> keys;
            if (error->GetOwnPropertyNames(context).ToLocal(&keys)) {
                for (uint32_t i = 0; i < keys->Length(); i++) {
                    Local<Value> key = keys->Get(context, i).ToLocalChecked();
                    Local<Value> value;
                    if (error->Get(context, key).ToLocal(&value) && !value->IsFunction() && !value->IsObject()) {
                        extra->Set(context, key, value).Check();
                    }
                }
            }
        }
        Local<Value> parts[2] = {exception, extra};
        message = Serialize(isolate, context, Array::New(isolate, parts, 2), Array::New(isolate));
    }
    if (message == nullptr) {
        // Not cloneable (a function, say): send what it looks like instead
        TryCatch try_catch(isolate);
        std::string text = frogjs::Inspect(isolate, context, exception);
        Local<Value> parts[2] = {V8String(isolate, text), Object::New(isolate)};
        message = Serialize(isolate, context, Array::New(isolate, parts, 2), Array::New(isolate));
    }
    std::lock_guard<std::mutex> lock(worker->mutex);
    if (worker->error == nullptr && !worker->exit_code_set) worker->error = std::move(message);
}

void OnStopAsync(uv_async_t* async) {
    uv_stop(async->loop);
}

void WorkerMain(void* arg) {
    std::shared_ptr<WorkerThread> worker = *static_cast<std::shared_ptr<WorkerThread>*>(arg);
    delete static_cast<std::shared_ptr<WorkerThread>*>(arg);

    uv_loop_t loop;
    uv_loop_init(&loop);
    frogjs::tls_loop = &loop;
    tls_worker = worker.get();

    uv_async_t stop_async;
    uv_async_init(&loop, &stop_async, OnStopAsync);
    uv_unref(reinterpret_cast<uv_handle_t*>(&stop_async));

    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = ArrayBuffer::Allocator::NewDefaultAllocator();
    Isolate* isolate = Isolate::New(create_params);

    // Terminated before it started: no JavaScript runs at all
    if (!worker->stopping) {
        Isolate::Scope isolate_scope(isolate);
        HandleScope handle_scope(isolate);
        std::string filename = worker->is_eval ? "[worker eval]" : worker->script;
        Local<Context> context = frogjs::CreateContext(isolate, worker->argv, frogjs::ExecArgv(), filename);
        Context::Scope context_scope(context);

        // Only now can terminate() interrupt it: stopping halfway through
        // setting up the built-in modules would leave them broken
        bool stopping;
        {
            std::lock_guard<std::mutex> lock(worker->mutex);
            stopping = worker->stopping;
            if (!stopping) {
                worker->isolate = isolate;
                worker->stop_async = &stop_async;
                worker->online = true;
            }
        }
        if (!stopping) {
            SignalParent(worker.get());

            std::string code = worker->script;
            std::string error;
            if (!worker->is_eval && !frogjs::ReadScriptFile(worker->script, &code, &error)) {
                Local<Object> exception = Exception::Error(
                    V8String(isolate, "Cannot find module '" + worker->script + "'")).As<Object>();
                exception->Set(context, String::NewFromUtf8Literal(isolate, "code"),
                    String::NewFromUtf8Literal(isolate, "MODULE_NOT_FOUND")).Check();
                frogjs::ForwardWorkerError(isolate, exception);
            } else if (frogjs::ExecuteScript(isolate, context, code, filename)) {
                while (!worker->stopping && uv_run(&loop, UV_RUN_DEFAULT)) {
                    isolate->PerformMicrotaskCheckpoint();
                }
            }
        }

        // No more JavaScript: close everything this thread owns
        {
            std::lock_guard<std::mutex> lock(worker->mutex);
            worker->isolate = nullptr;
            worker->stop_async = nullptr;
        }
        isolate->TerminateExecution();
        frogjs::TerminateAllWorkers();  // workers this worker created
        while (!g_thread_ports.empty()) ClosePort(g_thread_ports.back(), false);
        uv_walk(&loop, [](uv_handle_t* handle, void*) {
            if (!uv_is_closing(handle)) uv_close(handle, nullptr);
        }, nullptr);
        uv_run(&loop, UV_RUN_DEFAULT);

        g_port_template.Reset();
        g_port_emitter.Reset();
        g_worker_emitter.Reset();
    }

    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->isolate = nullptr;
        worker->stop_async = nullptr;
    }
    if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&stop_async))) {
        uv_close(reinterpret_cast<uv_handle_t*>(&stop_async), nullptr);
    }
    uv_run(&loop, UV_RUN_DEFAULT);
    isolate->Dispose();
    delete create_params.array_buffer_allocator;
    uv_loop_close(&loop);

    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->exited = true;
    }
    SignalParent(worker.get());
}

// Parent side: report what the worker did since the last signal
void OnWorkerAsync(uv_async_t* async) {
    WorkerHandle* handle = static_cast<WorkerHandle*>(async->data);
    std::shared_ptr<WorkerThread> worker = handle->worker;
    Isolate* isolate = handle->isolate;
    HandleScope handle_scope(isolate);
    Local<Context> context = handle->context.Get(isolate);
    Context::Scope context_scope(context);

    bool online;
    bool exited;
    int exit_code;
    std::unique_ptr<Message> error;
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        online = worker->online;
        exited = worker->exited;
        exit_code = worker->exit_code;
        if (!handle->error_emitted && worker->error != nullptr) error = std::move(worker->error);
    }

    Local<Object> object = handle->object.Get(isolate);
    if (online && !handle->online_emitted) {
        handle->online_emitted = true;
        Local<Value> argv[2] = {object, String::NewFromUtf8Literal(isolate, "online")};
        CallEmitter(isolate, context, g_worker_emitter, 2, argv);
    }
    if (error != nullptr) {
        handle->error_emitted = true;
        TryCatch try_catch(isolate);
        Local<Value> value;
        if (ReadMessage(isolate, context, error.get()).ToLocal(&value)) {
            Local<Value> argv[3] = {object, String::NewFromUtf8Literal(isolate, "error"), value};
            CallEmitter(isolate, context, g_worker_emitter, 3, argv);
        }
    }
    if (exited) {
        JoinWorker(worker.get());
        uv_close(reinterpret_cast<uv_handle_t*>(&handle->async), [](uv_handle_t* async) {
            delete static_cast<WorkerHandle*>(async->data);
        });
        Local<Value> argv[3] = {object, String::NewFromUtf8Literal(isolate, "exit"), Integer::New(isolate, exit_code)};
        CallEmitter(isolate, context, g_worker_emitter, 3, argv);
    }
}

std::vector<std::string> StringArray(Isolate* isolate, Local<Context> context, Local<Value> value) {
    std::vector<std::string> result;
    if (!value->IsArray()) return result;
    Local<Array> array = value.As<Array>();
    for (uint32_t i = 0; i < array->Length(); i++) {
        Local<Value> item;
        if (!array->Get(context, i).ToLocal(&item)) continue;
        String::Utf8Value utf8(isolate, item);
        result.emplace_back(*utf8 ? *utf8 : "");
    }
    return result;
}

WorkerThread* WorkerArg(Local<Value> value) {
    if (!value->IsObject() || value.As<Object>()->InternalFieldCount() < 1) return nullptr;
    Local<Value> field = value.As<Object>()->GetInternalField(0).As<Value>();
    if (!field->IsExternal()) return nullptr;
    WorkerHandle* handle = frogjs::ExternalValue<WorkerHandle>(field);
    return handle != nullptr ? handle->worker.get() : nullptr;
}

// createWorker(script, isEval, argv, workerData, transferList) ->
// { handle, port, threadId }
void CreateWorker(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    auto worker = std::make_shared<WorkerThread>();
    String::Utf8Value script(isolate, args[0]);
    worker->script = *script ? *script : "";
    worker->is_eval = args[1]->IsTrue();
    worker->worker_data = Serialize(isolate, context, args[3], args[4]);
    if (worker->worker_data == nullptr) return;
    worker->thread_id = g_next_thread_id++;
    worker->argv.push_back(frogjs::ExecPath());
    worker->argv.push_back(worker->is_eval ? "[worker eval]" : worker->script);
    for (std::string& arg : StringArray(isolate, context, args[2])) worker->argv.push_back(arg);
    worker->parent_loop = frogjs::Loop();

    auto [parent_end, worker_end] = NewChannel();
    worker->parent_port = worker_end;
    Local<Object> port = NewPort(isolate, context, parent_end);

    Local<ObjectTemplate> handle_template = ObjectTemplate::New(isolate);
    handle_template->SetInternalFieldCount(1);
    Local<Object> object = handle_template->NewInstance(context).ToLocalChecked();
    WorkerHandle* handle = new WorkerHandle;
    handle->isolate = isolate;
    handle->context.Reset(isolate, context);
    handle->object.Reset(isolate, object);
    handle->worker = worker;
    handle->async.data = handle;
    uv_async_init(frogjs::Loop(), &handle->async, OnWorkerAsync);
    object->SetInternalField(0, frogjs::NewExternal(isolate, handle));
    worker->parent = handle;

    // V8 needs more stack than a default secondary thread gets on macOS
    uv_thread_options_t options;
    options.flags = UV_THREAD_HAS_STACK_SIZE;
    options.stack_size = 4 * 1024 * 1024;
    auto* arg = new std::shared_ptr<WorkerThread>(worker);
    int status = uv_thread_create_ex(&worker->thread, &options, WorkerMain, arg);
    if (status != 0) {
        delete arg;
        worker->joined = true;
        {
            std::lock_guard<std::mutex> lock(worker->mutex);
            worker->exit_code_set = true;
            worker->exit_code = 1;
            worker->exited = true;
        }
        uv_async_send(&handle->async);
    } else {
        std::lock_guard<std::mutex> lock(g_workers_mutex);
        g_workers.push_back(worker);
    }

    Local<Object> result = Object::New(isolate);
    result->Set(context, String::NewFromUtf8Literal(isolate, "handle"), object).Check();
    result->Set(context, String::NewFromUtf8Literal(isolate, "port"), port).Check();
    result->Set(context, String::NewFromUtf8Literal(isolate, "threadId"), Integer::New(isolate, worker->thread_id)).Check();
    args.GetReturnValue().Set(result);
}

// terminate(handle): stop the worker; its 'exit' event follows (code 1)
void Terminate(const FunctionCallbackInfo<Value>& args) {
    WorkerThread* worker = WorkerArg(args[0]);
    if (worker != nullptr) StopWorker(worker, 1);
}

// workerRef(handle, ref): whether the worker keeps this thread's loop alive
void WorkerRef(const FunctionCallbackInfo<Value>& args) {
    WorkerThread* worker = WorkerArg(args[0]);
    if (worker == nullptr) return;
    uv_handle_t* async = reinterpret_cast<uv_handle_t*>(&worker->parent->async);
    if (uv_is_closing(async)) return;
    if (args[1]->IsTrue()) uv_ref(async);
    else uv_unref(async);
}

}  // namespace

namespace frogjs {

void TerminateAllWorkers() {
    // Only the workers this thread created: each worker stops its own
    std::vector<std::shared_ptr<WorkerThread>> workers;
    {
        std::lock_guard<std::mutex> lock(g_workers_mutex);
        for (auto& worker : g_workers) {
            if (worker->parent_loop == Loop()) workers.push_back(worker);
        }
    }
    for (auto& worker : workers) StopWorker(worker.get(), 1);
    for (auto& worker : workers) JoinWorker(worker.get());
}

void ExitWorker(Isolate* isolate, int exit_code) {
    if (tls_worker == nullptr) return;
    StopWorker(tls_worker, exit_code);
    isolate->TerminateExecution();
    uv_stop(Loop());
}

bool ForwardWorkerError(Isolate* isolate, Local<Value> exception) {
    if (tls_worker == nullptr) return false;
    HandleScope handle_scope(isolate);
    SetWorkerError(isolate, isolate->GetCurrentContext(), tls_worker, exception);
    StopWorker(tls_worker, 1);
    uv_stop(Loop());
    return true;
}

}  // namespace frogjs

// Set up worker_threads module (requires events and path), exposed as global
// `worker_threads` and returned by require('worker_threads')
void SetupWorkerThreads(Isolate* isolate, Local<Context> context) {
    Local<Object> global = context->Global();
    Local<Value> events = global->Get(context, String::NewFromUtf8Literal(isolate, "events")).ToLocalChecked();
    Local<Value> path = global->Get(context, String::NewFromUtf8Literal(isolate, "path")).ToLocalChecked();

    Local<FunctionTemplate> port_template = FunctionTemplate::New(isolate, PortConstructor);
    port_template->SetClassName(String::NewFromUtf8Literal(isolate, "MessagePort"));
    port_template->InstanceTemplate()->SetInternalFieldCount(1);
    g_port_template.Reset(isolate, port_template);

    Local<Object> native = Object::New(isolate);
    native->Set(context, String::NewFromUtf8Literal(isolate, "MessagePort"),
        port_template->GetFunction(context).ToLocalChecked()).Check();
    frogjs::SetFunction(isolate, context, native, "createChannel", CreateChannel);
    frogjs::SetFunction(isolate, context, native, "postMessage", PostMessage);
    frogjs::SetFunction(isolate, context, native, "start", Start);
    frogjs::SetFunction(isolate, context, native, "stop", Stop);
    frogjs::SetFunction(isolate, context, native, "ref", Ref);
    frogjs::SetFunction(isolate, context, native, "hasRef", HasRef);
    frogjs::SetFunction(isolate, context, native, "close", Close);
    frogjs::SetFunction(isolate, context, native, "drain", Drain);
    frogjs::SetFunction(isolate, context, native, "receiveMessage", ReceiveMessage);
    frogjs::SetFunction(isolate, context, native, "setPortEmitter", SetPortEmitter);
    frogjs::SetFunction(isolate, context, native, "setWorkerEmitter", SetWorkerEmitter);
    frogjs::SetFunction(isolate, context, native, "createWorker", CreateWorker);
    frogjs::SetFunction(isolate, context, native, "terminate", Terminate);
    frogjs::SetFunction(isolate, context, native, "workerRef", WorkerRef);

    WorkerThread* worker = tls_worker;
    native->Set(context, String::NewFromUtf8Literal(isolate, "isMainThread"), Boolean::New(isolate, worker == nullptr)).Check();
    native->Set(context, String::NewFromUtf8Literal(isolate, "threadId"),
        Integer::New(isolate, worker != nullptr ? worker->thread_id : 0)).Check();

    Local<Value> worker_data = Null(isolate);
    if (worker != nullptr) {
        // A context made again (none is, today) would get a closed port
        if (worker->parent_port != nullptr) {
            native->Set(context, String::NewFromUtf8Literal(isolate, "parentPort"),
                NewPort(isolate, context, std::move(worker->parent_port))).Check();
        }
        if (worker->worker_data != nullptr) {
            TryCatch try_catch(isolate);
            if (!ReadMessage(isolate, context, worker->worker_data.get()).ToLocal(&worker_data)) {
                worker_data = Undefined(isolate);
            }
            worker->worker_data.reset();
        }
    }
    native->Set(context, String::NewFromUtf8Literal(isolate, "workerData"), worker_data).Check();

    Local<Value> argv[3] = {native, events, path};
    Local<Value> worker_threads;
    if (!frogjs::RunJsModule(isolate, context, "worker_threads", kWorkerThreadsSource, 3, argv).ToLocal(&worker_threads)) {
        return;
    }
    global->Set(context, String::NewFromUtf8Literal(isolate, "worker_threads"), worker_threads).Check();
}
