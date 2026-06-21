#include <v8.h>
#include <uv.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

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

// ============================================
// Directory Operations
// ============================================

// fs.mkdirSync(path, mode)
void MkdirSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "mkdirSync requires a path").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);
    int mode = 0755;  // Default directory permissions

    if (args.Length() >= 2 && args[1]->IsNumber()) {
        mode = args[1]->Int32Value(isolate->GetCurrentContext()).ToChecked();
    }

    uv_fs_t req;
    int result = uv_fs_mkdir(uv_default_loop(), &req, *path, mode, nullptr);
    uv_fs_req_cleanup(&req);

    if (result < 0) {
        std::string error_msg = "Error creating directory: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// Callback for async mkdir
void MkdirCallback(uv_fs_t* req) {
    FileRequest* file_req = static_cast<FileRequest*>(req->data);
    Isolate* isolate = file_req->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    Local<Value> argv[1];

    if (req->result < 0) {
        std::string error_msg = "Error creating directory: ";
        error_msg += uv_strerror((int)req->result);
        argv[0] = String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked();
    } else {
        argv[0] = Null(isolate);
    }

    Local<Function> callback = Local<Function>::New(isolate, file_req->callback);
    TryCatch try_catch(isolate);
    callback->Call(context, context->Global(), 1, argv);

    if (try_catch.HasCaught()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        fprintf(stderr, "Callback error: %s\n", *error);
    }

    file_req->callback.Reset();
    uv_fs_req_cleanup(req);
    delete file_req;
}

// fs.mkdir(path, mode, callback)
void Mkdir(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 2 || !args[args.Length() - 1]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "mkdir requires a path and callback").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);
    int mode = 0755;  // Default permissions
    int callback_index = 1;

    if (args.Length() >= 3 && args[1]->IsNumber()) {
        mode = args[1]->Int32Value(isolate->GetCurrentContext()).ToChecked();
        callback_index = 2;
    }

    Local<Function> callback = Local<Function>::Cast(args[callback_index]);

    FileRequest* file_req = new FileRequest;
    file_req->req.data = file_req;
    file_req->isolate = isolate;
    file_req->filename = *path;
    file_req->callback.Reset(isolate, callback);

    int result = uv_fs_mkdir(uv_default_loop(), &file_req->req, *path, mode, MkdirCallback);

    if (result < 0) {
        file_req->callback.Reset();
        delete file_req;
        std::string error_msg = "Error creating directory: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// fs.readdirSync(path)
void ReaddirSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "readdirSync requires a path").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);

    uv_fs_t req;
    int result = uv_fs_scandir(uv_default_loop(), &req, *path, 0, nullptr);

    if (result < 0) {
        uv_fs_req_cleanup(&req);
        std::string error_msg = "Error reading directory: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
        return;
    }

    // Create array for directory entries
    Local<Array> files_array = Array::New(isolate);
    uv_dirent_t dirent;
    int i = 0;

    while (uv_fs_scandir_next(&req, &dirent) != UV_EOF) {
        Local<String> name = String::NewFromUtf8(isolate, dirent.name).ToLocalChecked();
        files_array->Set(context, i, name).Check();
        i++;
    }

    uv_fs_req_cleanup(&req);
    args.GetReturnValue().Set(files_array);
}

// Callback for async readdir
void ReaddirCallback(uv_fs_t* req) {
    FileRequest* file_req = static_cast<FileRequest*>(req->data);
    Isolate* isolate = file_req->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    Local<Value> argv[2];

    if (req->result < 0) {
        std::string error_msg = "Error reading directory: ";
        error_msg += uv_strerror((int)req->result);
        argv[0] = String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked();
        argv[1] = Null(isolate);
    } else {
        argv[0] = Null(isolate);

        // Create array for directory entries
        Local<Array> files_array = Array::New(isolate);
        uv_dirent_t dirent;
        int i = 0;

        while (uv_fs_scandir_next(req, &dirent) != UV_EOF) {
            Local<String> name = String::NewFromUtf8(isolate, dirent.name).ToLocalChecked();
            files_array->Set(context, i, name).Check();
            i++;
        }

        argv[1] = files_array;
    }

    Local<Function> callback = Local<Function>::New(isolate, file_req->callback);
    TryCatch try_catch(isolate);
    callback->Call(context, context->Global(), 2, argv);

    if (try_catch.HasCaught()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        fprintf(stderr, "Callback error: %s\n", *error);
    }

    file_req->callback.Reset();
    uv_fs_req_cleanup(req);
    delete file_req;
}

// fs.readdir(path, callback)
void Readdir(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 2 || !args[1]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "readdir requires a path and callback").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);
    Local<Function> callback = Local<Function>::Cast(args[1]);

    FileRequest* file_req = new FileRequest;
    file_req->req.data = file_req;
    file_req->isolate = isolate;
    file_req->filename = *path;
    file_req->callback.Reset(isolate, callback);

    int result = uv_fs_scandir(uv_default_loop(), &file_req->req, *path, 0, ReaddirCallback);

    if (result < 0) {
        file_req->callback.Reset();
        delete file_req;
        std::string error_msg = "Error reading directory: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// fs.rmdirSync(path)
void RmdirSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "rmdirSync requires a path").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);

    uv_fs_t req;
    int result = uv_fs_rmdir(uv_default_loop(), &req, *path, nullptr);
    uv_fs_req_cleanup(&req);

    if (result < 0) {
        std::string error_msg = "Error removing directory: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// Callback for async rmdir
void RmdirCallback(uv_fs_t* req) {
    FileRequest* file_req = static_cast<FileRequest*>(req->data);
    Isolate* isolate = file_req->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    Local<Value> argv[1];

    if (req->result < 0) {
        std::string error_msg = "Error removing directory: ";
        error_msg += uv_strerror((int)req->result);
        argv[0] = String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked();
    } else {
        argv[0] = Null(isolate);
    }

    Local<Function> callback = Local<Function>::New(isolate, file_req->callback);
    TryCatch try_catch(isolate);
    callback->Call(context, context->Global(), 1, argv);

    if (try_catch.HasCaught()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        fprintf(stderr, "Callback error: %s\n", *error);
    }

    file_req->callback.Reset();
    uv_fs_req_cleanup(req);
    delete file_req;
}

// fs.rmdir(path, callback)
void Rmdir(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 2 || !args[1]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "rmdir requires a path and callback").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);
    Local<Function> callback = Local<Function>::Cast(args[1]);

    FileRequest* file_req = new FileRequest;
    file_req->req.data = file_req;
    file_req->isolate = isolate;
    file_req->filename = *path;
    file_req->callback.Reset(isolate, callback);

    int result = uv_fs_rmdir(uv_default_loop(), &file_req->req, *path, RmdirCallback);

    if (result < 0) {
        file_req->callback.Reset();
        delete file_req;
        std::string error_msg = "Error removing directory: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// ============================================
// File Metadata Operations
// ============================================

// Helper function to create Stats object
Local<Object> CreateStatsObject(Isolate* isolate, Local<Context> context, uv_stat_t* stat) {
    Local<Object> stats = Object::New(isolate);

    stats->Set(context, String::NewFromUtf8(isolate, "dev").ToLocalChecked(),
        Number::New(isolate, stat->st_dev)).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "ino").ToLocalChecked(),
        Number::New(isolate, stat->st_ino)).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "mode").ToLocalChecked(),
        Number::New(isolate, stat->st_mode)).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "nlink").ToLocalChecked(),
        Number::New(isolate, stat->st_nlink)).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "uid").ToLocalChecked(),
        Number::New(isolate, stat->st_uid)).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "gid").ToLocalChecked(),
        Number::New(isolate, stat->st_gid)).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "rdev").ToLocalChecked(),
        Number::New(isolate, stat->st_rdev)).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "size").ToLocalChecked(),
        Number::New(isolate, stat->st_size)).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "blksize").ToLocalChecked(),
        Number::New(isolate, stat->st_blksize)).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "blocks").ToLocalChecked(),
        Number::New(isolate, stat->st_blocks)).Check();

    // Convert timestamps to Date objects (milliseconds since epoch)
    double atime_ms = stat->st_atim.tv_sec * 1000 + stat->st_atim.tv_nsec / 1000000.0;
    double mtime_ms = stat->st_mtim.tv_sec * 1000 + stat->st_mtim.tv_nsec / 1000000.0;
    double ctime_ms = stat->st_ctim.tv_sec * 1000 + stat->st_ctim.tv_nsec / 1000000.0;

    stats->Set(context, String::NewFromUtf8(isolate, "atime").ToLocalChecked(),
        Date::New(context, atime_ms).ToLocalChecked()).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "mtime").ToLocalChecked(),
        Date::New(context, mtime_ms).ToLocalChecked()).Check();
    stats->Set(context, String::NewFromUtf8(isolate, "ctime").ToLocalChecked(),
        Date::New(context, ctime_ms).ToLocalChecked()).Check();

    // Add isFile and isDirectory properties (not methods in Node.js style)
    bool is_file = S_ISREG(stat->st_mode);
    bool is_dir = S_ISDIR(stat->st_mode);

    stats->Set(context, String::NewFromUtf8(isolate, "isFile").ToLocalChecked(),
        Boolean::New(isolate, is_file)).Check();

    stats->Set(context, String::NewFromUtf8(isolate, "isDirectory").ToLocalChecked(),
        Boolean::New(isolate, is_dir)).Check();

    return stats;
}

// fs.statSync(path)
void StatSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "statSync requires a path").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);

    uv_fs_t req;
    int result = uv_fs_stat(uv_default_loop(), &req, *path, nullptr);

    if (result < 0) {
        uv_fs_req_cleanup(&req);
        std::string error_msg = "Error getting file stats: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
        return;
    }

    Local<Object> stats = CreateStatsObject(isolate, context, &req.statbuf);
    uv_fs_req_cleanup(&req);

    args.GetReturnValue().Set(stats);
}

// Callback for async stat
void StatCallback(uv_fs_t* req) {
    FileRequest* file_req = static_cast<FileRequest*>(req->data);
    Isolate* isolate = file_req->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    Local<Value> argv[2];

    if (req->result < 0) {
        std::string error_msg = "Error getting file stats: ";
        error_msg += uv_strerror((int)req->result);
        argv[0] = String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked();
        argv[1] = Null(isolate);
    } else {
        argv[0] = Null(isolate);
        Local<Object> stats = CreateStatsObject(isolate, context, &req->statbuf);
        argv[1] = stats;
    }

    Local<Function> callback = Local<Function>::New(isolate, file_req->callback);
    TryCatch try_catch(isolate);
    callback->Call(context, context->Global(), 2, argv);

    if (try_catch.HasCaught()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        fprintf(stderr, "Callback error: %s\n", *error);
    }

    file_req->callback.Reset();
    uv_fs_req_cleanup(req);
    delete file_req;
}

// fs.stat(path, callback)
void Stat(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 2 || !args[1]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "stat requires a path and callback").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);
    Local<Function> callback = Local<Function>::Cast(args[1]);

    FileRequest* file_req = new FileRequest;
    file_req->req.data = file_req;
    file_req->isolate = isolate;
    file_req->filename = *path;
    file_req->callback.Reset(isolate, callback);

    int result = uv_fs_stat(uv_default_loop(), &file_req->req, *path, StatCallback);

    if (result < 0) {
        file_req->callback.Reset();
        delete file_req;
        std::string error_msg = "Error getting file stats: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// fs.existsSync(path)
void ExistsSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 1) {
        args.GetReturnValue().Set(False(isolate));
        return;
    }

    String::Utf8Value path(isolate, args[0]);

    uv_fs_t req;
    int result = uv_fs_stat(uv_default_loop(), &req, *path, nullptr);
    uv_fs_req_cleanup(&req);

    args.GetReturnValue().Set(result >= 0);
}

// ============================================
// File Deletion Operations
// ============================================

// fs.unlinkSync(path)
void UnlinkSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "unlinkSync requires a path").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);

    uv_fs_t req;
    int result = uv_fs_unlink(uv_default_loop(), &req, *path, nullptr);
    uv_fs_req_cleanup(&req);

    if (result < 0) {
        std::string error_msg = "Error deleting file: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// Callback for async unlink
void UnlinkCallback(uv_fs_t* req) {
    FileRequest* file_req = static_cast<FileRequest*>(req->data);
    Isolate* isolate = file_req->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    Local<Value> argv[1];

    if (req->result < 0) {
        std::string error_msg = "Error deleting file: ";
        error_msg += uv_strerror((int)req->result);
        argv[0] = String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked();
    } else {
        argv[0] = Null(isolate);
    }

    Local<Function> callback = Local<Function>::New(isolate, file_req->callback);
    TryCatch try_catch(isolate);
    callback->Call(context, context->Global(), 1, argv);

    if (try_catch.HasCaught()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        fprintf(stderr, "Callback error: %s\n", *error);
    }

    file_req->callback.Reset();
    uv_fs_req_cleanup(req);
    delete file_req;
}

// fs.unlink(path, callback)
void Unlink(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 2 || !args[1]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "unlink requires a path and callback").ToLocalChecked());
        return;
    }

    String::Utf8Value path(isolate, args[0]);
    Local<Function> callback = Local<Function>::Cast(args[1]);

    FileRequest* file_req = new FileRequest;
    file_req->req.data = file_req;
    file_req->isolate = isolate;
    file_req->filename = *path;
    file_req->callback.Reset(isolate, callback);

    int result = uv_fs_unlink(uv_default_loop(), &file_req->req, *path, UnlinkCallback);

    if (result < 0) {
        file_req->callback.Reset();
        delete file_req;
        std::string error_msg = "Error deleting file: ";
        error_msg += uv_strerror(result);
        isolate->ThrowException(String::NewFromUtf8(isolate, error_msg.c_str()).ToLocalChecked());
    }
}

// Set up fs module
void SetupFileSystem(Isolate* isolate, Local<Context> context) {
    Local<Object> fs = Object::New(isolate);

    // ============================================
    // Original operations (read/write)
    // ============================================

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

    // ============================================
    // Directory operations
    // ============================================

    // fs.mkdirSync
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "mkdirSync").ToLocalChecked(),
        FunctionTemplate::New(isolate, MkdirSync)->GetFunction(context).ToLocalChecked()
    ).Check();

    // fs.mkdir
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "mkdir").ToLocalChecked(),
        FunctionTemplate::New(isolate, Mkdir)->GetFunction(context).ToLocalChecked()
    ).Check();

    // fs.readdirSync
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "readdirSync").ToLocalChecked(),
        FunctionTemplate::New(isolate, ReaddirSync)->GetFunction(context).ToLocalChecked()
    ).Check();

    // fs.readdir
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "readdir").ToLocalChecked(),
        FunctionTemplate::New(isolate, Readdir)->GetFunction(context).ToLocalChecked()
    ).Check();

    // fs.rmdirSync
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "rmdirSync").ToLocalChecked(),
        FunctionTemplate::New(isolate, RmdirSync)->GetFunction(context).ToLocalChecked()
    ).Check();

    // fs.rmdir
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "rmdir").ToLocalChecked(),
        FunctionTemplate::New(isolate, Rmdir)->GetFunction(context).ToLocalChecked()
    ).Check();

    // ============================================
    // File metadata operations
    // ============================================

    // fs.statSync
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "statSync").ToLocalChecked(),
        FunctionTemplate::New(isolate, StatSync)->GetFunction(context).ToLocalChecked()
    ).Check();

    // fs.stat
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "stat").ToLocalChecked(),
        FunctionTemplate::New(isolate, Stat)->GetFunction(context).ToLocalChecked()
    ).Check();

    // fs.existsSync
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "existsSync").ToLocalChecked(),
        FunctionTemplate::New(isolate, ExistsSync)->GetFunction(context).ToLocalChecked()
    ).Check();

    // ============================================
    // File deletion operations
    // ============================================

    // fs.unlinkSync
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "unlinkSync").ToLocalChecked(),
        FunctionTemplate::New(isolate, UnlinkSync)->GetFunction(context).ToLocalChecked()
    ).Check();

    // fs.unlink
    fs->Set(
        context,
        String::NewFromUtf8(isolate, "unlink").ToLocalChecked(),
        FunctionTemplate::New(isolate, Unlink)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Attach fs to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "fs").ToLocalChecked(),
        fs
    ).Check();
}
