# Building a JavaScript Runtime: FrogJS Development Guide

## Project Overview
FrogJS is a JavaScript runtime built on V8 (JavaScript engine) and libuv (async I/O library), inspired by Node.js architecture.

---

## Step-by-Step Implementation Guide

### Phase 1: Foundation Setup

#### Step 1: Project Structure Setup ✅
**Current Status**: Basic structure exists

Create the following directory structure:
```
frogjs/
├── src/
│   ├── core/           # Core runtime components
│   │   ├── runtime.cpp # Main entry point
│   │   ├── isolate.cpp # V8 isolate management
│   │   └── context.cpp # V8 context handling
│   ├── bindings/       # C++ to JS bindings
│   │   ├── console.cpp # console.log, console.error, etc.
│   │   ├── fs.cpp      # File system operations
│   │   ├── timers.cpp  # setTimeout, setInterval
│   │   └── net.cpp     # Network operations
│   ├── loop/           # Event loop integration
│   │   └── event_loop.cpp
│   └── utils/          # Helper utilities
│       └── helpers.cpp
├── include/            # Header files
│   └── frogjs/
├── lib/                # JavaScript standard library
│   ├── console.js
│   ├── fs.js
│   └── net.js
├── deps/               # Third-party dependencies
├── test/               # Test files
├── examples/           # Example scripts
├── build/              # Build output
└── bin/                # Executable output
```

**Action Required**: Create src/ directory with subdirectories

---

#### Step 2: Install Dependencies
**Prerequisites**:
- V8 JavaScript Engine
- libuv (async I/O)
- CMake or Make build system
- C++17 compatible compiler (GCC/Clang/MSVC)

**Installation Notes**:
- **V8**: Complex to build from source. Consider using prebuilt binaries or system package manager
  - macOS: `brew install v8`
  - Linux: Build from source or use distro packages
  - Windows: Use vcpkg or prebuilt binaries

- **libuv**: Event loop library
  - macOS: `brew install libuv`
  - Linux: `sudo apt-get install libuv1-dev` (Ubuntu/Debian)
  - Windows: vcpkg or manual build

---

### Phase 2: Core Runtime Implementation

#### Step 3: Create Main Runtime Entry Point
**File**: `src/core/runtime.cpp`

**What to implement**:
```cpp
// Key components:
1. Initialize V8 platform
2. Create V8 isolate
3. Set up V8 context
4. Read and execute JavaScript file
5. Integrate with libuv event loop
6. Handle cleanup on exit
```

**Important V8 Concepts**:
- **Isolate**: Independent instance of V8 engine (isolated heap)
- **Context**: Execution environment with global object
- **HandleScope**: Manages V8 object lifecycle
- **Persistent/Local handles**: References to JS objects

**Code Structure**:
```cpp
#include <v8.h>
#include <libplatform/libplatform.h>
#include <uv.h>

int main(int argc, char* argv[]) {
    // 1. Initialize V8
    v8::V8::InitializeICUDefaultLocation(argv[0]);
    v8::V8::InitializeExternalStartupData(argv[0]);
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();

    // 2. Create isolate
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator =
        v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    v8::Isolate* isolate = v8::Isolate::New(create_params);

    // 3. Enter isolate and create context
    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context);

        // 4. Set up bindings (console, fs, etc.)
        SetupBindings(isolate, context);

        // 5. Read and execute JavaScript file
        if (argc > 1) {
            std::string js_code = ReadFile(argv[1]);
            ExecuteScript(isolate, context, js_code);
        }

        // 6. Run event loop
        RunEventLoop();
    }

    // Cleanup
    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();

    return 0;
}
```

---

#### Step 4: Integrate libuv Event Loop
**File**: `src/loop/event_loop.cpp`

**Purpose**:
- Handle asynchronous operations (file I/O, timers, network)
- Keep runtime alive while async operations are pending
- Bridge between V8 and libuv

**Implementation Points**:
```cpp
// Initialize libuv loop
uv_loop_t* loop = uv_default_loop();

// Run loop until no more work
void RunEventLoop() {
    while (uv_run(loop, UV_RUN_DEFAULT)) {
        // Process V8 microtasks
        v8::MicrotasksScope::PerformCheckpoint(isolate);
    }
}

// Keep reference to isolate for callbacks
static v8::Isolate* g_isolate = nullptr;
```

**Key Concepts**:
- libuv runs in `UV_RUN_DEFAULT` mode (blocks until work is done)
- V8 microtasks (Promises) must be processed each iteration
- C++ callbacks must re-enter V8 context to call JavaScript

---

### Phase 3: JavaScript Bindings

#### Step 5: Implement Console Binding
**File**: `src/bindings/console.cpp`

**Functionality**: `console.log()`, `console.error()`, `console.warn()`

```cpp
void ConsoleLog(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);

    // Convert JS arguments to strings and print
    for (int i = 0; i < args.Length(); i++) {
        v8::String::Utf8Value str(isolate, args[i]);
        printf("%s ", *str);
    }
    printf("\n");
}

void SetupConsole(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> console = v8::Object::New(isolate);

    // Bind console.log
    console->Set(
        context,
        v8::String::NewFromUtf8(isolate, "log").ToLocalChecked(),
        v8::FunctionTemplate::New(isolate, ConsoleLog)
            ->GetFunction(context).ToLocalChecked()
    ).Check();

    // Attach console to global object
    context->Global()->Set(
        context,
        v8::String::NewFromUtf8(isolate, "console").ToLocalChecked(),
        console
    ).Check();
}
```

---

#### Step 6: Implement File System Binding
**File**: `src/bindings/fs.cpp`

**Functionality**:
- `readFile(path, callback)` - Async file read
- `readFileSync(path)` - Sync file read
- `writeFile(path, data, callback)` - Async file write
- `writeFileSync(path, data)` - Sync file write

**Async Example** (using libuv):
```cpp
struct ReadFileRequest {
    uv_fs_t req;
    v8::Persistent<v8::Function> callback;
    v8::Isolate* isolate;
};

void ReadFileCallback(uv_fs_t* req) {
    ReadFileRequest* request = static_cast<ReadFileRequest*>(req->data);
    v8::Isolate* isolate = request->isolate;
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    // Prepare callback arguments
    v8::Local<v8::Value> argv[2];
    if (req->result < 0) {
        // Error
        argv[0] = v8::String::NewFromUtf8(isolate, uv_strerror(req->result))
                    .ToLocalChecked();
        argv[1] = v8::Null(isolate);
    } else {
        // Success
        argv[0] = v8::Null(isolate);
        argv[1] = v8::String::NewFromUtf8(isolate, (char*)req->ptr)
                    .ToLocalChecked();
    }

    // Call JavaScript callback
    v8::Local<v8::Function> callback =
        v8::Local<v8::Function>::New(isolate, request->callback);
    callback->Call(context, context->Global(), 2, argv).ToLocalChecked();

    // Cleanup
    request->callback.Reset();
    uv_fs_req_cleanup(req);
    delete request;
}

void ReadFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::String::Utf8Value path(isolate, args[0]);

    ReadFileRequest* request = new ReadFileRequest;
    request->req.data = request;
    request->isolate = isolate;
    request->callback.Reset(isolate, args[1].As<v8::Function>());

    uv_fs_read(uv_default_loop(), &request->req, *path,
               ReadFileCallback);
}
```

---

#### Step 7: Implement Timer Binding
**File**: `src/bindings/timers.cpp`

**Functionality**:
- `setTimeout(callback, delay)`
- `setInterval(callback, delay)`
- `clearTimeout(id)`
- `clearInterval(id)`

**Implementation**:
```cpp
struct TimerData {
    v8::Persistent<v8::Function> callback;
    v8::Isolate* isolate;
    uv_timer_t handle;
    bool repeat;
};

void TimerCallback(uv_timer_t* handle) {
    TimerData* data = static_cast<TimerData*>(handle->data);
    v8::Isolate* isolate = data->isolate;
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    v8::Local<v8::Function> callback =
        v8::Local<v8::Function>::New(isolate, data->callback);
    callback->Call(context, context->Global(), 0, nullptr).ToLocalChecked();

    if (!data->repeat) {
        data->callback.Reset();
        uv_close((uv_handle_t*)handle, nullptr);
        delete data;
    }
}

void SetTimeout(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();

    TimerData* data = new TimerData;
    data->isolate = isolate;
    data->callback.Reset(isolate, args[0].As<v8::Function>());
    data->repeat = false;

    int delay = args[1]->Int32Value(isolate->GetCurrentContext()).ToChecked();

    uv_timer_init(uv_default_loop(), &data->handle);
    data->handle.data = data;
    uv_timer_start(&data->handle, TimerCallback, delay, 0);

    // Return timer ID (handle pointer as number)
    args.GetReturnValue().Set(
        v8::Number::New(isolate, (double)(intptr_t)&data->handle)
    );
}
```

---

#### Step 8: Implement Network Binding
**File**: `src/bindings/net.cpp`

**Functionality**:
- TCP server creation
- TCP client connections
- Socket read/write operations

**Basic TCP Server**:
```cpp
// Create server, bind to port, listen for connections
// Use uv_tcp_t for TCP operations
// Handle connection callbacks
// Implement read/write streams

void CreateServer(const v8::FunctionCallbackInfo<v8::Value>& args) {
    // 1. Create uv_tcp_t handle
    // 2. Bind to address and port
    // 3. Start listening
    // 4. On connection: call JS callback with socket object
}
```

---

### Phase 4: Module System

#### Step 9: Implement `require()` Function
**File**: `src/core/module_loader.cpp`

**Requirements**:
- Load JavaScript files
- Cache modules to prevent multiple loads
- Support relative paths (`./module.js`)
- Support built-in modules (`require('fs')`)

**Module Resolution**:
```cpp
1. Check if built-in module (fs, net, console, etc.)
2. Resolve relative path from current file
3. Read file contents
4. Wrap in module wrapper:
   (function(exports, require, module, __filename, __dirname) {
       // module code here
   });
5. Execute and return module.exports
6. Cache result
```

---

### Phase 5: Error Handling & Debugging

#### Step 10: Implement Error Handling
- Catch V8 exceptions: `v8::TryCatch`
- Print stack traces
- Handle uncaught exceptions
- Implement `process.on('uncaughtException')`

```cpp
v8::TryCatch try_catch(isolate);
v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

if (try_catch.HasCaught()) {
    v8::String::Utf8Value error(isolate, try_catch.Exception());
    v8::String::Utf8Value stack(isolate, try_catch.StackTrace(context)
                                            .ToLocalChecked());
    fprintf(stderr, "Error: %s\n%s\n", *error, *stack);
}
```

---

### Phase 6: Advanced Features

#### Step 11: Implement Global Objects
- `process` object (argv, env, exit, etc.)
- `global` object reference
- `Buffer` for binary data
- `__filename` and `__dirname`

#### Step 12: Promise Support
- Ensure V8 microtasks run properly
- Implement process.nextTick()
- Handle promise rejections

#### Step 13: Native Modules
- Allow C++ addons (.node files)
- Dynamic library loading

#### Step 14: Performance & Optimization
- V8 snapshots for faster startup
- JIT optimization hints
- Memory profiling

---

## Build System Notes

### Current Build Configuration
- **Makefile**: Uses g++ with manual linking
- **CMakeLists.txt**: Uses CMake with find_package for deps

### Recommended Build Steps
```bash
# Using Make
make clean
make
./build/frogjs script.js

# Using CMake
mkdir -p build && cd build
cmake ..
make
./frogjs script.js
```

---

## Testing Strategy

### Unit Tests
- Test each binding individually
- Mock libuv for deterministic testing

### Integration Tests
```javascript
// test/console.test.js
console.log("Hello, World!");

// test/timers.test.js
setTimeout(() => {
    console.log("Timer fired!");
}, 100);

// test/fs.test.js
const fs = require('fs');
fs.readFile('./test.txt', (err, data) => {
    if (err) throw err;
    console.log(data);
});
```

---

## Key Challenges & Solutions

### Challenge 1: V8 Version Compatibility
- **Problem**: V8 API changes between versions
- **Solution**: Pin to specific V8 version, document in README

### Challenge 2: Memory Management
- **Problem**: Leaking V8 handles or libuv resources
- **Solution**: Use HandleScope properly, cleanup in callbacks

### Challenge 3: Thread Safety
- **Problem**: V8 isolates are single-threaded
- **Solution**: Use Isolate locking, consider worker threads later

### Challenge 4: Error Context
- **Problem**: C++ exceptions vs JS exceptions
- **Solution**: Always use TryCatch, convert C++ errors to JS

---

## Additional Notes

### Performance Considerations
- V8 startup time: ~50-100ms (use snapshots to reduce)
- JIT warmup: First run slower than subsequent runs
- Event loop overhead: Minimal if using libuv efficiently

### Debugging Tips
- Use `--inspect` flag (requires DevTools protocol implementation)
- Add verbose logging in C++ callbacks
- Use `console.log()` liberally in JS
- GDB/LLDB for C++ debugging

### Next Steps After MVP
1. Add more FS operations (mkdir, unlink, stat, etc.)
2. Implement HTTP module (higher level than net)
3. Add stream API (Readable, Writable, Transform)
4. Create package manager integration (npm compatibility?)
5. Add REPL mode (interactive prompt)
6. Worker threads for parallel execution
7. Native ESM support (import/export)
8. TypeScript definitions for built-in modules

---

## Resources

### Documentation
- [V8 Embedder's Guide](https://v8.dev/docs/embed)
- [libuv Documentation](http://docs.libuv.org/)
- [Node.js C++ Addons Guide](https://nodejs.org/api/addons.html)

### Reference Implementations
- Node.js source code (large but comprehensive)
- Deno source code (modern, Rust-based but similar concepts)
- QuickJS (lightweight, good for learning)

### Books
- "Node.js Design Patterns" (understanding runtime architecture)
- "V8 JavaScript Engine Internals" (deep dive into V8)

---

## Current Project Status

✅ **Completed**:
- Project structure defined and created
- Build system configured (Makefile with C++20)
- Core runtime implemented with V8 and libuv integration
- Console bindings (console.log, console.error)
- Timer bindings (setTimeout, setInterval, clearTimeout, clearInterval)
- File system sync bindings (readFileSync, writeFileSync)
- File system async bindings (readFile, writeFile)
- Event loop integration (libuv UV_RUN_DEFAULT)
- Error handling with TryCatch and stack traces
- Example scripts created and tested
- Documentation completed

🔨 **In Progress**:
- Nothing currently

⏳ **To Do**:
- Add more FS operations (mkdir, unlink, stat, readdir, etc.)
- Implement HTTP module
- Add process object (argv, env, exit, cwd)
- Implement require() for module loading
- Add Buffer for binary data
- Network bindings (TCP/UDP)
- Stream API

---

## Quick Reference Commands

```bash
# Build
make clean && make

# Run example
./build/frogjs examples/hello.js

# Test
./build/frogjs test/console.test.js
./build/frogjs test/timers.test.js

# Debug
gdb ./build/frogjs
lldb ./build/frogjs
```

---

**Last Updated**: 2025-10-28
