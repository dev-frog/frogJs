#include <v8.h>
#include <uv.h>
#include <string>

using namespace v8;

// Structure to hold connection data
struct ConnectionData {
    uv_tcp_t handle;
    Persistent<Object> jsSocket;
    Isolate* isolate;
};

// Structure to hold server data
struct ServerData {
    uv_tcp_t handle;
    Persistent<Function> connectionCallback;
    Isolate* isolate;
};

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
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    Local<Object> socket = Local<Object>::New(isolate, conn->jsSocket);

    if (nread > 0) {
        // Data received
        Local<String> dataStr = String::NewFromUtf8(isolate, buf->base,
            NewStringType::kNormal, nread).ToLocalChecked();

        // Emit 'data' event
        Local<Value> onDataKey = String::NewFromUtf8(isolate, "_ondata").ToLocalChecked();
        Local<Value> onData = socket->Get(context, onDataKey).ToLocalChecked();

        if (onData->IsFunction()) {
            Local<Function> callback = Local<Function>::Cast(onData);
            Local<Value> argv[1] = { dataStr };
            callback->Call(context, socket, 1, argv).IsEmpty();
        }
    } else if (nread < 0) {
        // Connection closed or error
        if (nread != UV_EOF) {
            fprintf(stderr, "Read error: %s\n", uv_strerror(nread));
        }

        // Emit 'end' event
        Local<Value> onEndKey = String::NewFromUtf8(isolate, "_onend").ToLocalChecked();
        Local<Value> onEnd = socket->Get(context, onEndKey).ToLocalChecked();

        if (onEnd->IsFunction()) {
            Local<Function> callback = Local<Function>::Cast(onEnd);
            callback->Call(context, socket, 0, nullptr).IsEmpty();
        }

        uv_close((uv_handle_t*)stream, [](uv_handle_t* handle) {
            ConnectionData* conn = static_cast<ConnectionData*>(handle->data);
            conn->jsSocket.Reset();
            delete conn;
        });
    }

    if (buf->base) {
        delete[] buf->base;
    }
}

// Write callback
void OnWrite(uv_write_t* req, int status) {
    if (status < 0) {
        fprintf(stderr, "Write error: %s\n", uv_strerror(status));
    }
    delete req;
}

// Socket.write(data)
void SocketWrite(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "write requires data").ToLocalChecked());
        return;
    }

    Local<Object> self = args.This();
    Local<Data> handleData = self->GetInternalField(0);

    if (!handleData.IsEmpty()) {
        Local<Value> handleVal = handleData.As<Value>();
        if (handleVal->IsExternal()) {
            ConnectionData* conn = static_cast<ConnectionData*>(
                Local<External>::Cast(handleVal)->Value());

            String::Utf8Value str(isolate, args[0]);

            uv_write_t* req = new uv_write_t;
            uv_buf_t buf = uv_buf_init(strdup(*str), str.length());

            uv_write(req, (uv_stream_t*)&conn->handle, &buf, 1, [](uv_write_t* req, int status) {
                if (status < 0) {
                    fprintf(stderr, "Write error: %s\n", uv_strerror(status));
                }
                free(req->data);
                delete req;
            });
            req->data = buf.base;
        }
    }
}

// Socket.end()
void SocketEnd(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    Local<Object> self = args.This();
    Local<Data> handleData = self->GetInternalField(0);

    if (!handleData.IsEmpty()) {
        Local<Value> handleVal = handleData.As<Value>();
        if (handleVal->IsExternal()) {
            ConnectionData* conn = static_cast<ConnectionData*>(
                Local<External>::Cast(handleVal)->Value());
            uv_close((uv_handle_t*)&conn->handle, [](uv_handle_t* handle) {
                ConnectionData* conn = static_cast<ConnectionData*>(handle->data);
                conn->jsSocket.Reset();
                delete conn;
            });
        }
    }
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

// Create socket object
Local<Object> CreateSocket(Isolate* isolate, Local<Context> context, ConnectionData* conn) {
    Local<ObjectTemplate> socketTemplate = ObjectTemplate::New(isolate);
    socketTemplate->SetInternalFieldCount(1);

    Local<Object> socket = socketTemplate->NewInstance(context).ToLocalChecked();
    socket->SetInternalField(0, External::New(isolate, conn));

    // Add methods
    socket->Set(context, String::NewFromUtf8(isolate, "write").ToLocalChecked(),
        FunctionTemplate::New(isolate, SocketWrite)->GetFunction(context).ToLocalChecked()).Check();

    socket->Set(context, String::NewFromUtf8(isolate, "end").ToLocalChecked(),
        FunctionTemplate::New(isolate, SocketEnd)->GetFunction(context).ToLocalChecked()).Check();

    socket->Set(context, String::NewFromUtf8(isolate, "on").ToLocalChecked(),
        FunctionTemplate::New(isolate, SocketOn)->GetFunction(context).ToLocalChecked()).Check();

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
    ConnectionData* conn = new ConnectionData;
    conn->isolate = isolate;
    conn->handle.data = conn;

    uv_tcp_init(uv_default_loop(), &conn->handle);

    if (uv_accept(server, (uv_stream_t*)&conn->handle) == 0) {
        // Create socket object
        Local<Object> socket = CreateSocket(isolate, context, conn);
        conn->jsSocket.Reset(isolate, socket);

        // Start reading
        uv_read_start((uv_stream_t*)&conn->handle, AllocBuffer, OnRead);

        // Call connection callback
        Local<Function> callback = Local<Function>::New(isolate, serverData->connectionCallback);
        Local<Value> argv[1] = { socket };

        TryCatch try_catch(isolate);
        callback->Call(context, context->Global(), 1, argv);

        if (try_catch.HasCaught()) {
            String::Utf8Value error(isolate, try_catch.Exception());
            fprintf(stderr, "Connection callback error: %s\n", *error);
        }
    } else {
        uv_close((uv_handle_t*)&conn->handle, nullptr);
        delete conn;
    }
}

// Server.listen(port, callback)
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
    Local<Data> handleData = self->GetInternalField(0);

    if (handleData.IsEmpty()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Invalid server object").ToLocalChecked());
        return;
    }

    Local<Value> handleVal = handleData.As<Value>();
    if (!handleVal->IsExternal()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Invalid server object").ToLocalChecked());
        return;
    }

    ServerData* serverData = static_cast<ServerData*>(
        Local<External>::Cast(handleVal)->Value());

    int port = args[0]->Int32Value(context).ToChecked();
    Local<Function> callback = args.Length() > 1 && args[1]->IsFunction()
        ? Local<Function>::Cast(args[1]) : Local<Function>();

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", port, &addr);

    uv_tcp_bind(&serverData->handle, (const struct sockaddr*)&addr, 0);

    int result = uv_listen((uv_stream_t*)&serverData->handle, 128, OnConnection);

    if (result) {
        std::string error = "Listen error: " + std::string(uv_strerror(result));
        isolate->ThrowException(String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked());
        return;
    }

    // Call callback if provided
    if (!callback.IsEmpty()) {
        callback->Call(context, self, 0, nullptr).IsEmpty();
    }

    args.GetReturnValue().Set(self);
}

// Server.close()
void ServerClose(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    Local<Object> self = args.This();
    Local<Data> handleData = self->GetInternalField(0);

    if (!handleData.IsEmpty()) {
        Local<Value> handleVal = handleData.As<Value>();
        if (handleVal->IsExternal()) {
            ServerData* serverData = static_cast<ServerData*>(
                Local<External>::Cast(handleVal)->Value());

            uv_close((uv_handle_t*)&serverData->handle, [](uv_handle_t* handle) {
                ServerData* serverData = static_cast<ServerData*>(handle->data);
                serverData->connectionCallback.Reset();
                delete serverData;
            });
        }
    }
}

// net.createServer(callback)
void CreateServer(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1 || !args[0]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "createServer requires a callback function").ToLocalChecked());
        return;
    }

    // Create server data
    ServerData* serverData = new ServerData;
    serverData->isolate = isolate;
    serverData->connectionCallback.Reset(isolate, Local<Function>::Cast(args[0]));
    serverData->handle.data = serverData;

    uv_tcp_init(uv_default_loop(), &serverData->handle);

    // Create server object
    Local<ObjectTemplate> serverTemplate = ObjectTemplate::New(isolate);
    serverTemplate->SetInternalFieldCount(1);

    Local<Object> server = serverTemplate->NewInstance(context).ToLocalChecked();
    server->SetInternalField(0, External::New(isolate, serverData));

    // Add methods
    server->Set(context, String::NewFromUtf8(isolate, "listen").ToLocalChecked(),
        FunctionTemplate::New(isolate, ServerListen)->GetFunction(context).ToLocalChecked()).Check();

    server->Set(context, String::NewFromUtf8(isolate, "close").ToLocalChecked(),
        FunctionTemplate::New(isolate, ServerClose)->GetFunction(context).ToLocalChecked()).Check();

    args.GetReturnValue().Set(server);
}

// Callback for client connection
void OnConnect(uv_connect_t* req, int status) {
    ConnectionData* conn = static_cast<ConnectionData*>(req->data);
    Isolate* isolate = conn->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    Local<Object> socket = Local<Object>::New(isolate, conn->jsSocket);

    if (status < 0) {
        // Connection failed
        Local<Value> onErrorKey = String::NewFromUtf8(isolate, "_onerror").ToLocalChecked();
        Local<Value> onError = socket->Get(context, onErrorKey).ToLocalChecked();

        if (onError->IsFunction()) {
            Local<Function> callback = Local<Function>::Cast(onError);
            Local<Value> argv[1] = { String::NewFromUtf8(isolate,
                uv_strerror(status)).ToLocalChecked() };
            callback->Call(context, socket, 1, argv).IsEmpty();
        }

        uv_close((uv_handle_t*)&conn->handle, [](uv_handle_t* handle) {
            ConnectionData* conn = static_cast<ConnectionData*>(handle->data);
            conn->jsSocket.Reset();
            delete conn;
        });
    } else {
        // Connection successful
        Local<Value> onConnectKey = String::NewFromUtf8(isolate, "_onconnect").ToLocalChecked();
        Local<Value> onConnect = socket->Get(context, onConnectKey).ToLocalChecked();

        if (onConnect->IsFunction()) {
            Local<Function> callback = Local<Function>::Cast(onConnect);
            callback->Call(context, socket, 0, nullptr).IsEmpty();
        }

        // Start reading
        uv_read_start((uv_stream_t*)&conn->handle, AllocBuffer, OnRead);
    }

    delete req;
}

// net.connect(port, host, callback)
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
    std::string host = args.Length() > 1 && args[1]->IsString()
        ? std::string(*String::Utf8Value(isolate, args[1]))
        : "127.0.0.1";

    // Create connection
    ConnectionData* conn = new ConnectionData;
    conn->isolate = isolate;
    conn->handle.data = conn;

    uv_tcp_init(uv_default_loop(), &conn->handle);

    // Create socket object
    Local<Object> socket = CreateSocket(isolate, context, conn);
    conn->jsSocket.Reset(isolate, socket);

    // Connect
    struct sockaddr_in dest;
    uv_ip4_addr(host.c_str(), port, &dest);

    uv_connect_t* connect = new uv_connect_t;
    connect->data = conn;

    uv_tcp_connect(connect, &conn->handle, (const struct sockaddr*)&dest, OnConnect);

    args.GetReturnValue().Set(socket);
}

// Set up net module
void SetupNet(Isolate* isolate, Local<Context> context) {
    Local<Object> net = Object::New(isolate);

    // net.createServer
    net->Set(
        context,
        String::NewFromUtf8(isolate, "createServer").ToLocalChecked(),
        FunctionTemplate::New(isolate, CreateServer)->GetFunction(context).ToLocalChecked()
    ).Check();

    // net.connect
    net->Set(
        context,
        String::NewFromUtf8(isolate, "connect").ToLocalChecked(),
        FunctionTemplate::New(isolate, NetConnect)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Attach net to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "net").ToLocalChecked(),
        net
    ).Check();
}
