# FrogJS Architecture

## System Overview

FrogJS is a JavaScript runtime built on two key technologies:
- **V8 JavaScript Engine**: Executes JavaScript code
- **libuv**: Handles asynchronous I/O operations

The architecture follows the same fundamental design as Node.js but with a simplified, minimal implementation.

```
┌─────────────────────────────────────────────────────────┐
│                     JavaScript Code                      │
│              (User Applications & Modules)               │
└─────────────────────────┬───────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────┐
│                   V8 JavaScript Engine                   │
│  • Isolate (Execution Context)                           │
│  • Context (Global Object & Scope)                      │
│  • Heap & Memory Management                             │
│  • JIT Compilation                                      │
└─────────────────────────┬───────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────┐
│                   C++ Bindings Layer                     │
│  • Console, Timers, File System, Network               │
│  • Module System                                        │
│  • Error Handling                                       │
└─────────────────────────┬───────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────┐
│                      libuv Event Loop                    │
│  • File I/O                                             │
│  • Network I/O                                          │
│  • Timers                                               │
│  • Thread Pool                                          │
└─────────────────────────┬───────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────┐
│                   Operating System                       │
│  • File System                                          │
│  • Network Stack                                        │
│  • System Calls                                         │
└─────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Runtime (`src/core/runtime.cpp`)

The main entry point that orchestrates the entire system.

**Key Responsibilities:**
- Initialize V8 platform and create isolates
- Set up execution contexts
- Load and execute JavaScript files
- Manage the event loop
- Handle cleanup and shutdown

**Initialization Flow:**
```
main()
  ├── Initialize V8 Platform
  ├── Create Isolate
  ├── Create Context
  ├── Setup Bindings (Console, Timers, FS, Net, Modules)
  ├── Execute User Script
  ├── Run Event Loop (until no more work)
  └── Cleanup (Isolate, V8, libuv)
```

**V8 Objects:**
- **Isolate**: Independent V8 engine instance with own heap
- **Context**: Execution environment with global object
- **HandleScope**: Manages V8 object lifecycle
- **TryCatch**: Captures JavaScript exceptions

### 2. Bindings Layer (`src/bindings/`)

C++ functions that expose system functionality to JavaScript.

#### Console (`runtime.cpp`)
```cpp
void ConsoleLog(const FunctionCallbackInfo<Value>& args)
void ConsoleError(const FunctionCallbackInfo<Value>& args)
void SetupConsole(Isolate* isolate, Local<Context> context)
```

#### Timers (`src/bindings/timers.cpp`)
```cpp
struct TimerData {
    Persistent<Function> callback;
    uv_timer_t handle;
    bool repeat;
};

void SetTimeout(const FunctionCallbackInfo<Value>& args)
void SetInterval(const FunctionCallbackInfo<Value>& args)
void ClearTimeout(const FunctionCallbackInfo<Value>& args)
```

#### File System (`src/bindings/fs.cpp`)
```cpp
struct ReadFileRequest {
    uv_fs_t req;
    Persistent<Function> callback;
    Isolate* isolate;
};

void ReadFileSync(const FunctionCallbackInfo<Value>& args)
void WriteFileSync(const FunctionCallbackInfo<Value>& args)
void ReadFile(const FunctionCallbackInfo<Value>& args)
void WriteFile(const FunctionCallbackInfo<Value>& args)
```

#### Network (`src/bindings/net.cpp`)
```cpp
struct ConnectionData {
    uv_tcp_t handle;
    Persistent<Object> jsSocket;
    Isolate* isolate;
    bool closing;
};

struct ServerData {
    uv_tcp_t handle;
    Persistent<Function> connectionCallback;
    Isolate* isolate;
};

void CreateServer(const FunctionCallbackInfo<Value>& args)
void NetConnect(const FunctionCallbackInfo<Value>& args)
void SocketWrite(const FunctionCallbackInfo<Value>& args)
```

#### Modules (`src/bindings/modules.cpp`)
```cpp
std::map<std::string, Persistent<Object>> moduleCache;

void Require(const FunctionCallbackInfo<Value>& args)
std::string ResolveModulePath(const std::string& path)
std::string ReadModuleFile(const std::string& filepath)
```

### 3. Event Loop Integration

FrogJS uses libuv's event loop for asynchronous operations:

```cpp
// Run event loop until no more work
while (uv_run(uv_default_loop(), UV_RUN_DEFAULT)) {
    // Process V8 microtasks (Promises, nextTick)
    isolate->PerformMicrotaskCheckpoint();
}
```

**Event Loop Phases:**
1. **Timers**: Execute setTimeout/setInterval callbacks
2. **I/O Callbacks**: Handle file system and network operations
3. **Idle/Prepare**: Internal libuv phases
4. **Poll**: Get new I/O events
5. **Check**: execute setImmediate callbacks (not yet implemented)
6. **Close Callbacks**: Handle cleanup

### 4. Memory Management

#### V8 Handle Management
```cpp
{
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);

    // Local handles are automatically managed
    Local<String> str = String::NewFromUtf8(isolate, "Hello");

    // Persistent handles must be explicitly managed
    Persistent<Function> callback;
    callback.Reset(isolate, func);
    callback.Reset(); // Cleanup when done
}
```

#### libuv Resource Management
```cpp
// Always cleanup requests
uv_fs_req_cleanup(&req);

// Always close handles
uv_close((uv_handle_t*)handle, [](uv_handle_t* handle) {
    // Cleanup callback
    delete data;
});
```

## Module System Architecture

### Module Resolution Algorithm

```
require('./module')
  ├── 1. Is it a built-in module? (Not implemented yet)
  ├── 2. Resolve relative path from __dirname
  ├── 3. Try exact path
  ├── 4. Try with .js extension
  ├── 5. Try with /index.js
  ├── 6. Cache the module
  └── 7. Execute and return exports
```

### Module Wrapper Function

Each module is wrapped in a function:
```javascript
(function(exports, require, module, __filename, __dirname) {
    // Module code here
    module.exports = { /* exports */ };
});
```

### Module Caching

```cpp
std::map<std::string, Persistent<Object>> moduleCache;

// Check cache
auto cached = moduleCache.find(filepath);
if (cached != moduleCache.end()) {
    return cached->second; // Return cached module
}

// Cache after execution
moduleCache[filepath].Reset(isolate, exportsObj);
```

## Data Flow

### Synchronous Operations
```
JavaScript Call
  → C++ Binding Function
  → Direct Operation (e.g., file read)
  → Return Result to JavaScript
```

### Asynchronous Operations
```
JavaScript Call with Callback
  → C++ Binding Function
  → Start libuv Operation
  → Return Immediately
  → libuv Completes Operation
  → C++ Callback (in Event Loop)
  → Convert Result & Call JavaScript Callback
```

## Thread Safety

### Current Model: Single-Threaded
- V8 isolates are **not thread-safe**
- All JavaScript execution happens on main thread
- libuv thread pool used for file I/O
- Callbacks execute on main thread

### Thread Safety Considerations
```cpp
// ❌ NOT SAFE: Access isolate from multiple threads
// ❌ NOT SAFE: Share V8 handles across threads

// ✅ SAFE: Use Isolate::Scope when entering V8
// ✅ SAFE: Use HandleScope for object management
Isolate::Scope isolate_scope(isolate);
HandleScope handle_scope(isolate);
```

## Error Handling Architecture

### JavaScript Errors
```cpp
TryCatch try_catch(isolate);
if (!script->Run(context).ToLocal(&result)) {
    // Exception occurred
    String::Utf8Value error(isolate, try_catch.Exception());
    fprintf(stderr, "Error: %s\n", *error);

    // Print stack trace
    Local<Value> stack_trace;
    if (try_catch.StackTrace(context).ToLocal(&stack_trace)) {
        String::Utf8Value stack(isolate, stack_trace);
        fprintf(stderr, "%s\n", *stack);
    }
}
```

### C++ Error Handling
```cpp
// Convert C++ errors to JavaScript exceptions
if (error_occurred) {
    isolate->ThrowException(
        String::NewFromUtf8(isolate, "Error message").ToLocalChecked()
    );
    return;
}
```

## Performance Considerations

### V8 Optimization
- **JIT Compilation**: V8 compiles hot code to machine code
- **Hidden Classes**: Optimizes object property access
- **Inline Caching**: Caches property lookups
- **Memory Management**: Generational garbage collection

### libuv Performance
- **Thread Pool**: 4 threads for file I/O operations
- **Non-blocking I/O**: Efficient network operations
- **Event Loop**: Single-threaded async processing

### Memory Usage
- **Isolate Heap**: Default size ~1.5GB (configurable)
- **Handle Scope Limits**: Prevents handle leaks
- **Persistent Handles**: Manual management required

## Build System

### Makefile Build (macOS)
```makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall
V8_INCLUDE = -I/opt/homebrew/include/v8
LIBUV_INCLUDE = -I/opt/homebrew/include
LIBS = -lv8 -lvuv

frogjs: runtime.cpp bindings/*.cpp
    $(CXX) $(CXXFLAGS) $(V8_INCLUDE) $(LIBUV_INCLUDE) \
        src/core/runtime.cpp \
        src/bindings/timers.cpp \
        src/bindings/fs.cpp \
        src/bindings/net.cpp \
        src/bindings/modules.cpp \
        $(LIBS) -o build/frogjs
```

### CMake Build (Cross-platform)
```cmake
cmake_minimum_required(VERSION 3.10)
project(FrogJS)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(v8 REQUIRED)
find_package(libuv REQUIRED)

include_directories(${V8_INCLUDE_DIRS} ${LIBUV_INCLUDE_DIRS})
add_executable(frogjs src/core/runtime.cpp src/bindings/*.cpp)
target_link_libraries(frogjs ${V8_LIBRARIES} ${LIBUV_LIBRARIES})
```

## Future Architecture Improvements

### Planned Enhancements
1. **Process Object**: Add `process.argv`, `process.env`, `process.exit`
2. **Buffer Class**: Efficient binary data handling
3. **Stream API**: Unified interface for data streams
4. **HTTP Module**: Higher-level protocol support
5. **Worker Threads**: Multi-threaded JavaScript execution
6. **Snapshot Support**: Faster startup with precompiled snapshots

### Scalability Considerations
- **Cluster Mode**: Multiple processes (future)
- **Worker Threads**: Parallel JavaScript execution (future)
- **Memory Limits**: Configurable heap sizes (future)
- **Performance Monitoring**: Built-in profiling (future)

## Comparison with Node.js

### Simplicity vs. Features
```
FrogJS:
  + Minimal codebase (~2000 lines C++)
  + Easy to understand and modify
  + Fast startup
  - Limited feature set
  - No ecosystem compatibility

Node.js:
  + Complete feature set
  + npm ecosystem
  + Production hardened
  - Large codebase
  - Slower startup
  - Complex architecture
```

### Design Philosophy
- **FrogJS**: Educational, minimal, understandable
- **Node.js**: Production, feature-complete, compatible

## Security Considerations

### Current Security Model
- No sandboxing (all code has system access)
- No permission system
- Full file system access
- Full network access

### Future Security Enhancements
- Permission system (file system, network access)
- Sandboxed execution contexts
- Module validation
- Secure worker threads

---

**Last Updated:** 2025-10-28
**Architecture Version:** 0.1.0
