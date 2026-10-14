#include <v8.h>
#include "frogjs/v8_compat.h"
#include "frogjs/buffer.h"
#include <uv.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace v8;

struct ConnectRequest;

// Structure to hold connection data
struct ConnectionData {
    uv_tcp_t handle;
    Persistent<Object> jsSocket;
    Isolate* isolate;
    bool closing;           // end()/destroy() called: no more writes
    bool binary;            // setEncoding('latin1'): deliver data as byte strings
    bool allowHalfOpen;     // keep the write side open after the peer ends
    bool resolving;         // DNS lookup in flight (its callback still uses this)
    bool freeAfterResolve;  // closed while resolving: the lookup callback frees it
    ConnectRequest* connect;  // pending client connect, if any
};

// Client connect state: resolved addresses, tried in order until one succeeds
struct ConnectRequest {
    ConnectionData* conn;
    std::vector<sockaddr_storage> addrs;
    size_t next;
};

// Structure to hold server data
struct ServerData {
    uv_tcp_t handle;
    Persistent<Function> connectionCallback;
    Persistent<Function> closeCallback;
    Persistent<Object> jsServer;
    Isolate* isolate;
    bool allowHalfOpen;
    bool closing;
};

static ConnectionData* NewConnection(Isolate* isolate) {
    ConnectionData* conn = new ConnectionData;
    conn->isolate = isolate;
    conn->closing = false;
    conn->binary = false;
    conn->allowHalfOpen = false;
    conn->resolving = false;
    conn->freeAfterResolve = false;
    conn->connect = nullptr;
    uv_tcp_init(uv_default_loop(), &conn->handle);
    conn->handle.data = conn;
    return conn;
}

// Get the native object behind a socket/server, or nullptr once it has closed
template <typename T>
static T* GetNative(Local<Object> self) {
    if (self->InternalFieldCount() < 1) {
        return nullptr;
    }
    Local<Value> handleVal = self->GetInternalField(0).As<Value>();
    return handleVal->IsExternal() ? frogjs::ExternalValue<T>(handleVal) : nullptr;
}

static void ReportException(Isolate* isolate, TryCatch& try_catch) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Value> stack;
    if (try_catch.StackTrace(context).ToLocal(&stack) && stack->IsString()) {
        String::Utf8Value str(isolate, stack);
        fprintf(stderr, "Uncaught %s\n", *str);
    } else {
        String::Utf8Value str(isolate, try_catch.Exception());
        fprintf(stderr, "Uncaught %s\n", *str);
    }
}

// Build an Error with Node-style code/syscall properties from a libuv status
static Local<Value> UVException(Isolate* isolate, int status, const char* syscall) {
    Local<Context> context = isolate->GetCurrentContext();
    std::string message = std::string(syscall) + " " + uv_err_name(status) + ": " + uv_strerror(status);
    Local<Object> error = Exception::Error(
        String::NewFromUtf8(isolate, message.c_str()).ToLocalChecked()).As<Object>();
    error->Set(context, String::NewFromUtf8(isolate, "code").ToLocalChecked(),
        String::NewFromUtf8(isolate, uv_err_name(status)).ToLocalChecked()).Check();
    error->Set(context, String::NewFromUtf8(isolate, "syscall").ToLocalChecked(),
        String::NewFromUtf8(isolate, syscall).ToLocalChecked()).Check();
    return error;
}

// Call socket._on<event>(...argv) if registered. Returns false if there was no handler.
static bool EmitSocketEvent(Isolate* isolate, Local<Object> socket, const char* event,
                            int argc, Local<Value> argv[]) {
    Local<Context> context = isolate->GetCurrentContext();
    std::string key = std::string("_on") + event;
    Local<Value> handler;
    if (!socket->Get(context, String::NewFromUtf8(isolate, key.c_str()).ToLocalChecked()).ToLocal(&handler) ||
        !handler->IsFunction()) {
        return false;
    }

    TryCatch try_catch(isolate);
    if (handler.As<Function>()->Call(context, socket, argc, argv).IsEmpty() && try_catch.HasCaught()) {
        ReportException(isolate, try_catch);
    }
    return true;
}

static void EmitSocketError(Isolate* isolate, Local<Object> socket, int status, const char* syscall) {
    Local<Value> argv[1] = { UVException(isolate, status, syscall) };
    if (!EmitSocketEvent(isolate, socket, "error", 1, argv)) {
        fprintf(stderr, "Socket error: %s: %s\n", syscall, uv_strerror(status));
    }
}

static Local<Object> GetSocket(ConnectionData* conn) {
    return Local<Object>::New(conn->isolate, conn->jsSocket);
}

// Final close callback: detach the JS object, emit 'close', free the connection
static void OnConnectionClosed(uv_handle_t* handle) {
    ConnectionData* conn = static_cast<ConnectionData*>(handle->data);
    Isolate* isolate = conn->isolate;
    HandleScope handle_scope(isolate);

    Local<Object> socket = GetSocket(conn);
    // Later calls on the JS object must not touch freed memory
    socket->SetInternalField(0, Undefined(isolate));
    conn->jsSocket.Reset();

    EmitSocketEvent(isolate, socket, "close", 0, nullptr);

    if (conn->resolving) {
        conn->freeAfterResolve = true;
    } else {
        delete conn;
    }
}

// Close immediately, discarding pending writes
static void CloseConnection(ConnectionData* conn) {
    conn->closing = true;
    if (!uv_is_closing((uv_handle_t*)&conn->handle)) {
        uv_close((uv_handle_t*)&conn->handle, OnConnectionClosed);
    }
}

// Close gracefully: flush pending writes, send FIN, then close
static void EndConnection(ConnectionData* conn) {
    if (conn->closing) {
        return;
    }
    conn->closing = true;
    uv_read_stop((uv_stream_t*)&conn->handle);

    uv_shutdown_t* req = new uv_shutdown_t;
    req->data = conn;
    int result = uv_shutdown(req, (uv_stream_t*)&conn->handle, [](uv_shutdown_t* req, int /*status*/) {
        ConnectionData* conn = static_cast<ConnectionData*>(req->data);
        delete req;
        CloseConnection(conn);
    });
    if (result != 0) {
        delete req;
        CloseConnection(conn);
    }
}

// Set remoteAddress/remotePort on the JS socket
static void SetPeerInfo(Isolate* isolate, Local<Object> socket, ConnectionData* conn) {
    Local<Context> context = isolate->GetCurrentContext();
    sockaddr_storage addr;
    int len = sizeof(addr);
    if (uv_tcp_getpeername(&conn->handle, (sockaddr*)&addr, &len) != 0) {
        return;
    }

    char ip[INET6_ADDRSTRLEN] = {0};
    int port = 0;
    if (addr.ss_family == AF_INET6) {
        sockaddr_in6* a = (sockaddr_in6*)&addr;
        uv_ip6_name(a, ip, sizeof(ip));
        port = ntohs(a->sin6_port);
    } else {
        sockaddr_in* a = (sockaddr_in*)&addr;
        uv_ip4_name(a, ip, sizeof(ip));
        port = ntohs(a->sin_port);
    }

    socket->Set(context, String::NewFromUtf8(isolate, "remoteAddress").ToLocalChecked(),
        String::NewFromUtf8(isolate, ip).ToLocalChecked()).Check();
    socket->Set(context, String::NewFromUtf8(isolate, "remotePort").ToLocalChecked(),
        Integer::New(isolate, port)).Check();
}

// Allocate buffer for reading
void AllocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = new char[suggested_size];
    buf->len = suggested_size;
}

// Called when data is received on a socket
void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    ConnectionData* conn = static_cast<ConnectionData*>(stream->data);
    Isolate* isolate = conn->isolate;
    HandleScope handle_scope(isolate);

    Local<Object> socket = GetSocket(conn);

    if (nread > 0) {
        // Emit 'data' event
        Local<String> data = conn->binary
            ? String::NewFromOneByte(isolate, (const uint8_t*)buf->base,
                NewStringType::kNormal, nread).ToLocalChecked()
            : String::NewFromUtf8(isolate, buf->base,
                NewStringType::kNormal, nread).ToLocalChecked();
        Local<Value> argv[1] = { data };
        EmitSocketEvent(isolate, socket, "data", 1, argv);
    } else if (nread < 0) {
        uv_read_stop(stream);

        if (nread == UV_EOF) {
            // Emit 'end' event; unless half-open is allowed, finish our side too
            EmitSocketEvent(isolate, socket, "end", 0, nullptr);
            if (!conn->allowHalfOpen) {
                EndConnection(conn);
            }
        } else {
            EmitSocketError(isolate, socket, nread, "read");
            CloseConnection(conn);
        }
    }

    if (buf->base) {
        delete[] buf->base;
    }
}

// Write callback
void OnWrite(uv_write_t* req, int status) {
    free(req->data);
    uv_stream_t* stream = req->handle;
    delete req;

    if (status < 0 && status != UV_ECANCELED && !uv_is_closing((uv_handle_t*)stream)) {
        ConnectionData* conn = static_cast<ConnectionData*>(stream->data);
        HandleScope handle_scope(conn->isolate);
        EmitSocketError(conn->isolate, GetSocket(conn), status, "write");
        CloseConnection(conn);
    }
}

// Queue data on the socket. Strings are UTF-8 unless encoding is 'latin1'/'binary'
// (one byte per char code); Buffers are written as raw bytes.
static bool WriteData(Isolate* isolate, ConnectionData* conn, Local<Value> value, Local<Value> encoding) {
    Local<Context> context = isolate->GetCurrentContext();
    char* data = nullptr;
    size_t length = 0;

    if (IsBuffer(value)) {
        BufferData* buffer_data = GetBufferData(value.As<Object>());
        length = buffer_data ? buffer_data->length : 0;
        if (length > 0) {
            data = (char*)malloc(length);
            memcpy(data, buffer_data->data, length);
        }
    } else {
        String::Utf8Value enc(isolate, encoding);
        bool latin1 = encoding->IsString() && (strcmp(*enc, "latin1") == 0 || strcmp(*enc, "binary") == 0);
        Local<String> str;
        if (!value->ToString(context).ToLocal(&str)) {
            return false;
        }

        if (latin1) {
            length = str->Length();
            if (length > 0) {
                data = (char*)malloc(length);
                frogjs::WriteOneByte(isolate, str, (uint8_t*)data);
            }
        } else {
            String::Utf8Value utf8(isolate, str);
            length = utf8.length();
            if (length > 0) {
                data = (char*)malloc(length);
                memcpy(data, *utf8, length);
            }
        }
    }

    if (length == 0) {
        return true;
    }

    uv_write_t* req = new uv_write_t;
    req->data = data;
    uv_buf_t buf = uv_buf_init(data, length);
    int result = uv_write(req, (uv_stream_t*)&conn->handle, &buf, 1, OnWrite);
    if (result != 0) {
        free(data);
        delete req;
        EmitSocketError(isolate, GetSocket(conn), result, "write");
        return false;
    }
    return true;
}

// Socket.write(data[, encoding])
void SocketWrite(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "write requires data").ToLocalChecked());
        return;
    }

    ConnectionData* conn = GetNative<ConnectionData>(args.This());

    // Don't write if the socket is closing
    if (conn == nullptr || conn->closing) {
        args.GetReturnValue().Set(false);
        return;
    }

    args.GetReturnValue().Set(WriteData(isolate, conn, args[0], args[1]));
}

// Socket.end([data][, encoding]) - flush pending writes, then close
void SocketEnd(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    ConnectionData* conn = GetNative<ConnectionData>(args.This());
    if (conn == nullptr || conn->closing) {
        return;
    }

    if (args.Length() > 0 && !args[0]->IsUndefined() && !args[0]->IsNull()) {
        WriteData(isolate, conn, args[0], args[1]);
    }
    EndConnection(conn);
}

// Socket.destroy() - close immediately
void SocketDestroy(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    ConnectionData* conn = GetNative<ConnectionData>(args.This());
    if (conn != nullptr) {
        CloseConnection(conn);
    }
}

// Socket.setEncoding(encoding) - 'utf8' (default) or 'latin1'/'binary'
void SocketSetEncoding(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    ConnectionData* conn = GetNative<ConnectionData>(args.This());
    if (conn != nullptr) {
        String::Utf8Value enc(isolate, args[0]);
        conn->binary = args[0]->IsString() &&
            (strcmp(*enc, "latin1") == 0 || strcmp(*enc, "binary") == 0);
    }
    args.GetReturnValue().Set(args.This());
}

// Socket.on(event, callback)
void SocketOn(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "on requires event name and callback").ToLocalChecked());
        return;
    }

    Local<Object> self = args.This();
    String::Utf8Value event(isolate, args[0]);
    Local<Function> callback = Local<Function>::Cast(args[1]);

    // Store callback as internal property
    std::string key = "_on" + std::string(*event);
    self->Set(context, String::NewFromUtf8(isolate, key.c_str()).ToLocalChecked(), callback).Check();

    args.GetReturnValue().Set(self);
}

static void SetMethod(Isolate* isolate, Local<Context> context, Local<Object> target,
                      const char* name, FunctionCallback callback) {
    target->Set(context, String::NewFromUtf8(isolate, name).ToLocalChecked(),
        FunctionTemplate::New(isolate, callback)->GetFunction(context).ToLocalChecked()).Check();
}

// Create socket object
Local<Object> CreateSocket(Isolate* isolate, Local<Context> context, ConnectionData* conn) {
    Local<ObjectTemplate> socketTemplate = ObjectTemplate::New(isolate);
    socketTemplate->SetInternalFieldCount(1);

    Local<Object> socket = socketTemplate->NewInstance(context).ToLocalChecked();
    socket->SetInternalField(0, frogjs::NewExternal(isolate, conn));

    // Add methods
    SetMethod(isolate, context, socket, "write", SocketWrite);
    SetMethod(isolate, context, socket, "end", SocketEnd);
    SetMethod(isolate, context, socket, "destroy", SocketDestroy);
    SetMethod(isolate, context, socket, "setEncoding", SocketSetEncoding);
    SetMethod(isolate, context, socket, "on", SocketOn);

    return socket;
}

// Called when a new connection is received
void OnConnection(uv_stream_t* server, int status) {
    if (status < 0) {
        fprintf(stderr, "Connection error: %s\n", uv_strerror(status));
        return;
    }

    ServerData* serverData = static_cast<ServerData*>(server->data);
    Isolate* isolate = serverData->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    // Create new connection
    ConnectionData* conn = NewConnection(isolate);
    conn->allowHalfOpen = serverData->allowHalfOpen;

    if (uv_accept(server, (uv_stream_t*)&conn->handle) == 0) {
        // Create socket object
        Local<Object> socket = CreateSocket(isolate, context, conn);
        conn->jsSocket.Reset(isolate, socket);
        SetPeerInfo(isolate, socket, conn);

        // Start reading
        uv_read_start((uv_stream_t*)&conn->handle, AllocBuffer, OnRead);

        // Call connection callback
        Local<Function> callback = Local<Function>::New(isolate, serverData->connectionCallback);
        Local<Value> argv[1] = { socket };

        TryCatch try_catch(isolate);
        callback->Call(context, context->Global(), 1, argv);

        if (try_catch.HasCaught()) {
            ReportException(isolate, try_catch);
        }
    } else {
        uv_close((uv_handle_t*)&conn->handle, [](uv_handle_t* handle) {
            delete static_cast<ConnectionData*>(handle->data);
        });
    }
}

// Server.listen(port[, host][, callback])
void ServerListen(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "listen requires port number").ToLocalChecked());
        return;
    }

    Local<Object> self = args.This();
    ServerData* serverData = GetNative<ServerData>(self);

    if (serverData == nullptr || serverData->closing) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate,
            "Server is closed").ToLocalChecked()));
        return;
    }

    int port = args[0]->Int32Value(context).ToChecked();
    if (port < 0 || port > 65535) {
        isolate->ThrowException(Exception::RangeError(String::NewFromUtf8(isolate,
            "Port should be >= 0 and < 65536").ToLocalChecked()));
        return;
    }

    int next = 1;
    std::string host = "0.0.0.0";
    if (args.Length() > next && args[next]->IsString()) {
        host = *String::Utf8Value(isolate, args[next]);
        if (host == "localhost") {
            host = "127.0.0.1";
        }
        next++;
    }
    Local<Function> callback = args.Length() > next && args[next]->IsFunction()
        ? Local<Function>::Cast(args[next]) : Local<Function>();

    sockaddr_storage addr;
    int result = host.find(':') != std::string::npos
        ? uv_ip6_addr(host.c_str(), port, (sockaddr_in6*)&addr)
        : uv_ip4_addr(host.c_str(), port, (sockaddr_in*)&addr);

    if (result == 0) {
        result = uv_tcp_bind(&serverData->handle, (const struct sockaddr*)&addr, 0);
    }
    if (result == 0) {
        result = uv_listen((uv_stream_t*)&serverData->handle, 128, OnConnection);
    }

    if (result) {
        isolate->ThrowException(UVException(isolate, result, "listen"));
        return;
    }

    // Call callback if provided
    if (!callback.IsEmpty()) {
        callback->Call(context, self, 0, nullptr).IsEmpty();
    }

    args.GetReturnValue().Set(self);
}

// Server.address() - { address, family, port } of the bound socket, or null
void ServerAddress(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    args.GetReturnValue().SetNull();
    ServerData* serverData = GetNative<ServerData>(args.This());
    if (serverData == nullptr) {
        return;
    }

    sockaddr_storage addr;
    int len = sizeof(addr);
    if (uv_tcp_getsockname(&serverData->handle, (sockaddr*)&addr, &len) != 0) {
        return;
    }

    char ip[INET6_ADDRSTRLEN] = {0};
    int port = 0;
    const char* family = "IPv4";
    if (addr.ss_family == AF_INET6) {
        uv_ip6_name((sockaddr_in6*)&addr, ip, sizeof(ip));
        port = ntohs(((sockaddr_in6*)&addr)->sin6_port);
        family = "IPv6";
    } else {
        uv_ip4_name((sockaddr_in*)&addr, ip, sizeof(ip));
        port = ntohs(((sockaddr_in*)&addr)->sin_port);
    }

    Local<Object> result = Object::New(isolate);
    result->Set(context, String::NewFromUtf8(isolate, "address").ToLocalChecked(),
        String::NewFromUtf8(isolate, ip).ToLocalChecked()).Check();
    result->Set(context, String::NewFromUtf8(isolate, "family").ToLocalChecked(),
        String::NewFromUtf8(isolate, family).ToLocalChecked()).Check();
    result->Set(context, String::NewFromUtf8(isolate, "port").ToLocalChecked(),
        Integer::New(isolate, port)).Check();
    args.GetReturnValue().Set(result);
}

// Server.close([callback]) - stop accepting connections
void ServerClose(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    ServerData* serverData = GetNative<ServerData>(args.This());
    if (serverData == nullptr || serverData->closing) {
        return;
    }

    serverData->closing = true;
    if (args.Length() > 0 && args[0]->IsFunction()) {
        serverData->closeCallback.Reset(isolate, Local<Function>::Cast(args[0]));
    }

    uv_close((uv_handle_t*)&serverData->handle, [](uv_handle_t* handle) {
        ServerData* serverData = static_cast<ServerData*>(handle->data);
        Isolate* isolate = serverData->isolate;
        HandleScope handle_scope(isolate);
        Local<Context> context = isolate->GetCurrentContext();

        Local<Object> server = Local<Object>::New(isolate, serverData->jsServer);
        server->SetInternalField(0, Undefined(isolate));

        if (!serverData->closeCallback.IsEmpty()) {
            Local<Function> callback = Local<Function>::New(isolate, serverData->closeCallback);
            TryCatch try_catch(isolate);
            if (callback->Call(context, server, 0, nullptr).IsEmpty() && try_catch.HasCaught()) {
                ReportException(isolate, try_catch);
            }
        }

        serverData->connectionCallback.Reset();
        serverData->closeCallback.Reset();
        serverData->jsServer.Reset();
        delete serverData;
    });
}

// net.createServer([options], callback)
void CreateServer(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    bool allowHalfOpen = false;
    int callbackIndex = 0;
    if (args.Length() > 1 && args[0]->IsObject() && !args[0]->IsFunction()) {
        Local<Value> option = args[0].As<Object>()->Get(context,
            String::NewFromUtf8(isolate, "allowHalfOpen").ToLocalChecked()).ToLocalChecked();
        allowHalfOpen = option->BooleanValue(isolate);
        callbackIndex = 1;
    }

    if (args.Length() <= callbackIndex || !args[callbackIndex]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "createServer requires a callback function").ToLocalChecked());
        return;
    }

    // Create server data
    ServerData* serverData = new ServerData;
    serverData->isolate = isolate;
    serverData->connectionCallback.Reset(isolate, Local<Function>::Cast(args[callbackIndex]));
    serverData->allowHalfOpen = allowHalfOpen;
    serverData->closing = false;
    serverData->handle.data = serverData;

    uv_tcp_init(uv_default_loop(), &serverData->handle);

    // Create server object
    Local<ObjectTemplate> serverTemplate = ObjectTemplate::New(isolate);
    serverTemplate->SetInternalFieldCount(1);

    Local<Object> server = serverTemplate->NewInstance(context).ToLocalChecked();
    server->SetInternalField(0, frogjs::NewExternal(isolate, serverData));
    serverData->jsServer.Reset(isolate, server);

    // Add methods
    SetMethod(isolate, context, server, "listen", ServerListen);
    SetMethod(isolate, context, server, "address", ServerAddress);
    SetMethod(isolate, context, server, "close", ServerClose);

    args.GetReturnValue().Set(server);
}

static void TryConnect(ConnectRequest* cr);

// Emit 'error' for a connection that could not be established, then close it
static void FailConnect(ConnectionData* conn, int status, const char* syscall) {
    HandleScope handle_scope(conn->isolate);
    EmitSocketError(conn->isolate, GetSocket(conn), status, syscall);
    CloseConnection(conn);
}

// A connect attempt failed: try the next resolved address, or give up
static void ConnectAttemptFailed(ConnectRequest* cr, int status) {
    ConnectionData* conn = cr->conn;
    if (conn->closing || cr->next >= cr->addrs.size()) {
        conn->connect = nullptr;
        delete cr;
        if (!conn->closing) {
            FailConnect(conn, status, "connect");
        }
        return;
    }

    // A failed connect leaves the handle unusable: close it and start over with a fresh one
    uv_close((uv_handle_t*)&conn->handle, [](uv_handle_t* handle) {
        ConnectionData* conn = static_cast<ConnectionData*>(handle->data);
        if (conn->closing) {
            // destroy()/end() was called meanwhile
            delete conn->connect;
            conn->connect = nullptr;
            OnConnectionClosed(handle);
            return;
        }
        uv_tcp_init(uv_default_loop(), &conn->handle);
        conn->handle.data = conn;
        TryConnect(conn->connect);
    });
}

// Callback for client connection
void OnConnect(uv_connect_t* req, int status) {
    ConnectRequest* cr = static_cast<ConnectRequest*>(req->data);
    ConnectionData* conn = cr->conn;
    delete req;

    if (status == UV_ECANCELED || conn->closing) {
        // Socket was destroyed while connecting; its close callback cleans up
        conn->connect = nullptr;
        delete cr;
        return;
    }

    if (status < 0) {
        ConnectAttemptFailed(cr, status);
        return;
    }

    conn->connect = nullptr;
    delete cr;

    Isolate* isolate = conn->isolate;
    HandleScope handle_scope(isolate);
    Local<Object> socket = GetSocket(conn);
    SetPeerInfo(isolate, socket, conn);

    // Emit 'connect' event
    EmitSocketEvent(isolate, socket, "connect", 0, nullptr);

    // Start reading
    if (!conn->closing) {
        uv_read_start((uv_stream_t*)&conn->handle, AllocBuffer, OnRead);
    }
}

static void TryConnect(ConnectRequest* cr) {
    ConnectionData* conn = cr->conn;
    uv_connect_t* req = new uv_connect_t;
    req->data = cr;

    const sockaddr* addr = (const sockaddr*)&cr->addrs[cr->next++];
    int result = uv_tcp_connect(req, &conn->handle, addr, OnConnect);
    if (result != 0) {
        delete req;
        ConnectAttemptFailed(cr, result);
    }
}

// DNS lookup finished: connect to the results, IPv4 first (matches the default
// 0.0.0.0 listen address), falling back to the next address on failure
static void OnResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    ConnectionData* conn = static_cast<ConnectionData*>(req->data);
    delete req;
    conn->resolving = false;

    if (conn->freeAfterResolve) {
        // Socket was destroyed and fully closed while resolving
        uv_freeaddrinfo(res);
        delete conn;
        return;
    }
    if (conn->closing) {
        uv_freeaddrinfo(res);
        return;
    }
    if (status < 0) {
        FailConnect(conn, status, "getaddrinfo");
        return;
    }

    ConnectRequest* cr = new ConnectRequest{conn, {}, 0};
    for (int family : {AF_INET, AF_INET6}) {
        for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
            if (ai->ai_family == family) {
                sockaddr_storage addr;
                memset(&addr, 0, sizeof(addr));
                memcpy(&addr, ai->ai_addr, ai->ai_addrlen);
                cr->addrs.push_back(addr);
            }
        }
    }
    uv_freeaddrinfo(res);

    if (cr->addrs.empty()) {
        delete cr;
        FailConnect(conn, UV_EAI_NONAME, "getaddrinfo");
        return;
    }

    conn->connect = cr;
    TryConnect(cr);
}

// net.connect(port[, host][, callback])
void NetConnect(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "connect requires port").ToLocalChecked());
        return;
    }

    int port = args[0]->Int32Value(context).ToChecked();
    if (port < 0 || port > 65535) {
        isolate->ThrowException(Exception::RangeError(String::NewFromUtf8(isolate,
            "Port should be >= 0 and < 65536").ToLocalChecked()));
        return;
    }

    int next = 1;
    std::string host = "127.0.0.1";
    if (args.Length() > next && args[next]->IsString()) {
        host = *String::Utf8Value(isolate, args[next]);
        next++;
    }

    // Create connection
    ConnectionData* conn = NewConnection(isolate);

    // Create socket object
    Local<Object> socket = CreateSocket(isolate, context, conn);
    conn->jsSocket.Reset(isolate, socket);

    // Optional callback is a 'connect' listener
    if (args.Length() > next && args[next]->IsFunction()) {
        socket->Set(context, String::NewFromUtf8(isolate, "_onconnect").ToLocalChecked(), args[next]).Check();
    }

    // Resolve the host (also handles IP literals), then connect
    uv_getaddrinfo_t* req = new uv_getaddrinfo_t;
    req->data = conn;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    int result = uv_getaddrinfo(uv_default_loop(), req, OnResolved, host.c_str(), portStr.c_str(), &hints);
    if (result != 0) {
        delete req;
        CloseConnection(conn);
        isolate->ThrowException(UVException(isolate, result, "getaddrinfo"));
        return;
    }
    conn->resolving = true;

    args.GetReturnValue().Set(socket);
}

// Set up net module
void SetupNet(Isolate* isolate, Local<Context> context) {
    Local<Object> net = Object::New(isolate);

    // net.createServer
    SetMethod(isolate, context, net, "createServer", CreateServer);

    // net.connect
    SetMethod(isolate, context, net, "connect", NetConnect);

    // Attach net to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "net").ToLocalChecked(),
        net
    ).Check();
}
