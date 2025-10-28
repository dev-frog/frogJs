#include <v8.h>
#include <uv.h>
#include <fstream>
#include <sstream>

using namespace v8;

// Structure for async file operations
struct FileRequest {
    uv_fs_t req;
    Persistent<Function> callback;
    Isolate* isolate;
    std::string filename;
    std::string data;
};

// Callback for async readFile
void ReadFileCallback(uv_fs_t* req) {
    FileRequest* file_req = static_cast<FileRequest*>(req->data);
    Isolate* isolate = file_req->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    Local<Value> argv[2];

    if (req->result < 0) {
        // Error occurred
        std::string error_msg = "Error reading file: ";
        error_msg += uv_strerror((int)req->result);
        argv[0] = String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked();
        argv[1] = Null(isolate);
    } else {
        // Success - read the file content
        uv_buf_t buffer = uv_buf_init(new char[req->result + 1], req->result);
        uv_fs_t read_req;
        int bytes_read = uv_fs_read(uv_default_loop(), &read_req, req->result, &buffer, 1, 0, nullptr);

        if (bytes_read >= 0) {
            buffer.base[bytes_read] = '\0';
            argv[0] = Null(isolate);
            argv[1] = String::NewFromUtf8(isolate, buffer.base).ToLocalChecked();
        } else {
            argv[0] = String::NewFromUtf8(isolate, "Error reading file content").ToLocalChecked();
            argv[1] = Null(isolate);
        }

        delete[] buffer.base;
        uv_fs_req_cleanup(&read_req);
        uv_fs_close(uv_default_loop(), &read_req, req->result, nullptr);
    }

    // Call the JavaScript callback
    Local<Function> callback = Local<Function>::New(isolate, file_req->callback);
    TryCatch try_catch(isolate);
    callback->Call(context, context->Global(), 2, argv);

    if (try_catch.HasCaught()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        fprintf(stderr, "Callback error: %s\n", *error);
    }

    // Cleanup
    file_req->callback.Reset();
    uv_fs_req_cleanup(req);
    delete file_req;
}

// Callback for async writeFile
void WriteFileCallback(uv_fs_t* req) {
    FileRequest* file_req = static_cast<FileRequest*>(req->data);
    Isolate* isolate = file_req->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    Local<Value> argv[1];

    if (req->result < 0) {
        // Error occurred
        std::string error_msg = "Error writing file: ";
        error_msg += uv_strerror((int)req->result);
        argv[0] = String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked();
    } else {
        argv[0] = Null(isolate);
    }

    // Call the JavaScript callback
    Local<Function> callback = Local<Function>::New(isolate, file_req->callback);
    TryCatch try_catch(isolate);
    callback->Call(context, context->Global(), 1, argv);

    if (try_catch.HasCaught()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        fprintf(stderr, "Callback error: %s\n", *error);
    }

    // Cleanup
    file_req->callback.Reset();
    uv_fs_req_cleanup(req);
    delete file_req;
}

// fs.readFileSync(path)
void ReadFileSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "readFileSync requires a file path").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);

    // Read file using C++ streams
    std::ifstream file(*path, std::ios::binary);
    if (!file.is_open()) {
        std::string error_msg = "Cannot open file: ";
        error_msg += *path;
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    args.GetReturnValue().Set(String::NewFromUtf8(isolate, content.c_str()).ToLocalChecked());
}

// fs.writeFileSync(path, data)
void WriteFileSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 2) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "writeFileSync requires a file path and data").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);
    String::Utf8Value data(isolate, args[1]);

    // Write file using C++ streams
    std::ofstream file(*path, std::ios::binary);
    if (!file.is_open()) {
        std::string error_msg = "Cannot open file for writing: ";
        error_msg += *path;
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
        return;
    }

    file.write(*data, data.length());
    file.close();

    if (file.fail()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Error writing to file").ToLocalChecked());
        return;
    }
}

// fs.readFile(path, callback)
void ReadFile(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 2 || !args[1]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "readFile requires a file path and callback").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);
    Local<Function> callback = Local<Function>::Cast(args[1]);

    // Create request
    FileRequest* file_req = new FileRequest;
    file_req->req.data = file_req;
    file_req->isolate = isolate;
    file_req->filename = *path;
    file_req->callback.Reset(isolate, callback);

    // Open file asynchronously
    int result = uv_fs_open(uv_default_loop(), &file_req->req, *path,
                           O_RDONLY, 0, ReadFileCallback);

    if (result < 0) {
        file_req->callback.Reset();
        delete file_req;
        std::string error_msg = "Error opening file: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// fs.writeFile(path, data, callback)
void WriteFile(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 3 || !args[2]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "writeFile requires a file path, data, and callback").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);
    String::Utf8Value data(isolate, args[1]);
    Local<Function> callback = Local<Function>::Cast(args[2]);

    // Create request
    FileRequest* file_req = new FileRequest;
    file_req->req.data = file_req;
    file_req->isolate = isolate;
    file_req->filename = *path;
    file_req->data = *data;
    file_req->callback.Reset(isolate, callback);

    // Write using simpler approach: open, write, close
    uv_fs_t open_req;
    int fd = uv_fs_open(uv_default_loop(), &open_req, *path,
                        O_WRONLY | O_CREAT | O_TRUNC, 0644, nullptr);
    uv_fs_req_cleanup(&open_req);

    if (fd < 0) {
        file_req->callback.Reset();
        delete file_req;
        std::string error_msg = "Error opening file: ";
        error_msg += uv_strerror(fd);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
        return;
    }

    uv_buf_t buffer = uv_buf_init(const_cast<char*>(file_req->data.c_str()), file_req->data.length());
    int result = uv_fs_write(uv_default_loop(), &file_req->req, fd, &buffer, 1, -1, WriteFileCallback);

    if (result < 0) {
        file_req->callback.Reset();
        delete file_req;
        uv_fs_t close_req;
        uv_fs_close(uv_default_loop(), &close_req, fd, nullptr);
        uv_fs_req_cleanup(&close_req);
        std::string error_msg = "Error writing file: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// Set up fs module
void SetupFileSystem(Isolate* isolate, Local<Context> context) {
    Local<Object> fs = Object::New(isolate);

    // Sync operations
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "readFileSync").ToLocalChecked(),
        FunctionTemplate::New(isolate, ReadFileSync)->GetFunction(context).ToLocalChecked()
    ).Check();

    fs->Set(
        context,
        String::NewFromUtf8(isolate, "writeFileSync").ToLocalChecked(),
        FunctionTemplate::New(isolate, WriteFileSync)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Async operations
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "readFile").ToLocalChecked(),
        FunctionTemplate::New(isolate, ReadFile)->GetFunction(context).ToLocalChecked()
    ).Check();

    fs->Set(
        context,
        String::NewFromUtf8(isolate, "writeFile").ToLocalChecked(),
        FunctionTemplate::New(isolate, WriteFile)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Attach fs to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "fs").ToLocalChecked(),
        fs
    ).Check();
}
