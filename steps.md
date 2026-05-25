# FrogJS Development Steps

A comprehensive guide to building FrogJS from scratch or understanding the implementation.

## Phase 1: Project Setup ✅

### Step 1: Create Directory Structure
```bash
# Core directories
mkdir -p src/core src/bindings src/loop src/utils
mkdir -p include/frogjs
mkdir -p lib examples test bin build docs

# Verify structure
tree -L 2
```

Expected structure:
```
frogjs/
├── src/
│   ├── core/          # Main runtime
│   ├── bindings/      # C++ to JS bridges
│   ├── loop/          # Event loop (optional)
│   └── utils/         # Helper utilities
├── include/           # Header files
├── lib/               # JavaScript stdlib (optional)
├── examples/          # Example scripts
├── test/              # Test files
├── docs/              # Documentation
├── build/             # Build output
└── bin/               # Executable output
```

### Step 2: Install Dependencies

#### macOS (Homebrew)
```bash
# Install V8 and libuv
brew install v8 libuv

# Verify installations
brew list v8
brew list libuv

# Check versions
brew info v8
brew info libuv

# Find include paths
brew --prefix v8     # typically /opt/homebrew/opt/v8
brew --prefix libuv  # typically /opt/homebrew/opt/libuv
```

#### Linux (Ubuntu/Debian)
```bash
# Install libuv
sudo apt-get update
sudo apt-get install libuv1-dev build-essential

# V8 requires manual installation or using distro packages
# Option 1: Build V8 from source (complex)
# Option 2: Use Node.js's V8 (complex)
# Option 3: Check distro packages
apt search v8

# Verify libuv
pkg-config --cflags --libs libuv
```

#### Version Requirements
- **V8**: 8.x+ (check compatibility)
- **libuv**: 1.x+
- **C++ Compiler**: C++17 support required

## Phase 2: Core Runtime ✅

### Step 3: Create Basic Runtime (runtime.cpp)

Start with minimal V8 initialization:

```cpp
#include <v8.h>
#include <libplatform/libplatform.h>
#include <uv.h>

using namespace v8;

int main(int argc, char* argv[]) {
    // 1. Initialize V8
    V8::InitializeICUDefaultLocation(argv[0]);
    V8::InitializeExternalStartupData(argv[0]);
    std::unique_ptr<Platform> platform = platform::NewDefaultPlatform();
    V8::InitializePlatform(platform.get());
    V8::Initialize();

    // 2. Create Isolate
    Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = ArrayBuffer::Allocator::NewDefaultAllocator();
    Isolate* isolate = Isolate::New(create_params);

    {
        Isolate::Scope isolate_scope(isolate);
        HandleScope handle_scope(isolate);

        // 3. Create Context
        Local<Context> context = Context::New(isolate);
        Context::Scope context_scope(context);

        // 4. Execute code
        Local<String> source = String::NewFromUtf8(isolate, "console.log('Hello, World!')").ToLocalChecked();
        Local<Script> script = Script::Compile(context, source).ToLocalChecked();
        script->Run(context).ToLocalChecked();
    }

    // 5. Cleanup
    isolate->Dispose();
    V8::Dispose();
    V8::DisposePlatform();
    delete create_params.array_buffer_allocator;

    return 0;
}
```

### Step 4: Add Console Bindings

```cpp
void ConsoleLog(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    for (int i = 0; i < args.Length(); i++) {
        if (i > 0) printf(" ");
        String::Utf8Value str(isolate, args[i]);
        printf("%s", *str);
    }
    printf("\n");
    fflush(stdout);
}

void SetupConsole(Isolate* isolate, Local<Context> context) {
    Local<Object> console = Object::New(isolate);

    console->Set(context,
        String::NewFromUtf8(isolate, "log").ToLocalChecked(),
        FunctionTemplate::New(isolate, ConsoleLog)->GetFunction(context).ToLocalChecked()
    ).Check();

    context->Global()->Set(context,
        String::NewFromUtf8(isolate, "console").ToLocalChecked(),
        console
    ).Check();
}
```

### Step 5: Create Makefile

```makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall -g
V8_PATH = /opt/homebrew/opt/v8
LIBUV_PATH = /opt/homebrew/opt/libuv

INCLUDES = -I$(V8_PATH)/include -I$(LIBUV_PATH)/include
LIBS = -L$(V8_PATH)/lib -lv8 -lv8_libplatform -L$(LIBUV_PATH)/lib -luv

all:
    $(CXX) $(CXXFLAGS) $(INCLUDES) src/core/runtime.cpp $(LIBS) -o build/frogjs

clean:
    rm -f build/frogjs
```

### Step 6: Test Basic Runtime

```bash
# Build
make clean && make

# Test
./build/frogjs examples/hello.js
```

## Phase 3: Timer Bindings ✅

### Step 7: Implement Timers (src/bindings/timers.cpp)

Create timer structures and functions:

```cpp
#include <v8.h>
#include <uv.h>
#include <map>

using namespace v8;

struct TimerData {
    Persistent<Function> callback;
    uv_timer_t handle;
    bool repeat;
};

std::map<uv_timer_t*, TimerData*> timerMap;

void TimerCallback(uv_timer_t* handle) {
    TimerData* data = static_cast<TimerData*>(handle->data);
    Isolate* isolate = data->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    Local<Function> callback = Local<Function>::New(isolate, data->callback);
    callback->Call(context, context->Global(), 0, nullptr).ToLocalChecked();

    if (!data->repeat) {
        data->callback.Reset();
        uv_close((uv_handle_t*)handle, nullptr);
        delete data;
    }
}

void SetTimeout(const FunctionCallbackInfo<Value>& args) {
    // Implementation...
}
```

## Phase 4: File System Bindings ✅

### Step 8: Implement File System (src/bindings/fs.cpp)

#### Sync Operations
```cpp
void ReadFileSync(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    String::Utf8Value path(isolate, args[0]);

    std::ifstream file(*path);
    if (!file.is_open()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Cannot open file").ToLocalChecked());
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    args.GetReturnValue().Set(
        String::NewFromUtf8(isolate, buffer.str().c_str()).ToLocalChecked()
    );
}
```

#### Async Operations
```cpp
struct ReadFileRequest {
    uv_fs_t req;
    Persistent<Function> callback;
    Isolate* isolate;
    std::string content;
};

void ReadFileCallback(uv_fs_t* req) {
    ReadFileRequest* request = static_cast<ReadFileRequest*>(req->data);
    Isolate* isolate = request->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    Local<Function> callback = Local<Function>::New(isolate, request->callback);
    Local<Value> argv[2] = {
        Null(isolate),
        String::NewFromUtf8(isolate, request->content.c_str()).ToLocalChecked()
    };

    callback->Call(context, context->Global(), 2, argv).ToLocalChecked();

    request->callback.Reset();
    uv_fs_req_cleanup(req);
    delete request;
}
```

## Phase 5: Network Bindings ✅

### Step 9: Implement TCP Server (src/bindings/net.cpp)

```cpp
struct ServerData {
    uv_tcp_t handle;
    Persistent<Function> connectionCallback;
    Isolate* isolate;
};

void OnConnection(uv_stream_t* server, int status) {
    // Handle new connection
}

void CreateServer(const FunctionCallbackInfo<Value>& args) {
    // Create server object
}
```

## Phase 6: Module System ✅

### Step 10: Implement Module Loading (src/bindings/modules.cpp)

```cpp
std::map<std::string, Persistent<Object>> moduleCache;

void Require(const FunctionCallbackInfo<Value>& args) {
    // 1. Resolve module path
    // 2. Check cache
    // 3. Read file
    // 4. Wrap in function
    // 5. Execute
    // 6. Cache result
}
```

## Phase 7: Build System

### Step 11: Update Makefile for Multiple Files

```makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall -g
V8_PATH = /opt/homebrew/opt/v8
LIBUV_PATH = /opt/homebrew/opt/libuv

INCLUDES = -I$(V8_PATH)/include -I$(LIBUV_PATH)/include
LIBS = -L$(V8_PATH)/lib -lv8 -lv8_libplatform -L$(LIBUV_PATH)/lib -luv

SOURCES = src/core/runtime.cpp \
          src/bindings/timers.cpp \
          src/bindings/fs.cpp \
          src/bindings/net.cpp \
          src/bindings/modules.cpp

all:
    $(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) $(LIBS) -o build/frogjs

clean:
    rm -f build/frogjs
```

### Step 12: Add CMake Support

```cmake
cmake_minimum_required(VERSION 3.10)
project(FrogJS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUV REQUIRED libuv)

# V8 configuration (may need custom FindV8.cmake)
set(V8_PATH "/opt/homebrew/opt/v8")
include_directories(${V8_PATH}/include ${LIBUV_INCLUDE_DIRS})

# Sources
file(GLOB SOURCES "src/**/*.cpp")

# Executable
add_executable(frogjs ${SOURCES})
target_link_libraries(frogjs ${LIBUV_LIBRARIES} ${V8_PATH}/lib/libv8.a)
```

## Phase 8: Testing and Examples

### Step 13: Create Test Examples

```bash
# Create test files
cat > examples/hello.js << 'EOF'
console.log("Hello, FrogJS!");
EOF

cat > examples/timers.js << 'EOF'
setTimeout(() => {
    console.log("Delayed execution");
}, 1000);

console.log("Immediate execution");
EOF

cat > examples/fs-sync.js << 'EOF'
const fs = require('fs');
const data = fs.readFileSync('test.txt');
console.log(data);
EOF
```

### Step 14: Test Each Feature

```bash
# Console
./build/frogjs examples/hello.js

# Timers
./build/frogjs examples/timers.js

# File System
echo "Test content" > test.txt
./build/frogjs examples/fs-sync.js

# TCP Server
./build/frogjs examples/tcp-server.js
# In another terminal: telnet localhost 3000
```

## Phase 9: Documentation

### Step 15: Create Documentation

```bash
# API Documentation
cat > docs/api-reference.md << 'EOF'
# FrogJS API Reference
...
EOF

# Architecture Documentation
cat > docs/architecture.md << 'EOF'
# FrogJS Architecture
...
EOF

# Contributing Guide
cat > docs/contributing.md << 'EOF'
# Contributing to FrogJS
...
EOF

# Update README
cat > README.md << 'EOF'
# FrogJS 🐸
...
EOF
```

## Phase 10: Advanced Features (Future)

### Step 16: Add Process Object
```cpp
// Implement process.argv, process.env, process.exit
```

### Step 17: Implement Buffer Class
```cpp
// Create Buffer for binary data handling
```

### Step 18: Add More FS Operations
```cpp
// mkdir, rmdir, stat, readdir, etc.
```

### Step 19: Implement HTTP Module
```cpp
// HTTP on top of TCP
```

### Step 20: Add REPL Mode
```cpp
// Interactive JavaScript shell
```

## Development Workflow

### Daily Development
```bash
# 1. Make changes
vim src/core/runtime.cpp

# 2. Build
make clean && make

# 3. Test
./build/frogjs examples/hello.js

# 4. Debug (if needed)
lldb ./build/frogjs -- examples/hello.js

# 5. Commit
git add .
git commit -m "feat: add new feature"
```

### Debugging Tips
```bash
# Use gdb/lldb for C++ debugging
lldb ./build/frogjs -- examples/hello.js

# Common breakpoints
(lldb) breakpoint set --name main
(lldb) breakpoint set --name ConsoleLog

# Print V8 values
(lldb) expr isolate->GetCurrentContext()
```

### Memory Profiling
```bash
# Use Valgrind for memory leak detection
valgrind --leak-check=full ./build/frogjs examples/hello.js

# Use Address Sanitizer
make CXXFLAGS="-std=c++17 -Wall -g -fsanitize=address"
```

## Troubleshooting

### Common Build Errors

#### V8 not found
```bash
# Check V8 installation
brew list v8

# Update paths in Makefile
V8_PATH = /custom/path/to/v8
```

#### libuv linking errors
```bash
# Verify libuv installation
pkg-config --cflags --libs libuv

# Update LIBUV_PATH in Makefile
```

#### C++17 errors
```bash
# Ensure compiler supports C++17
g++ --version  # Should be 7.0+

# Update CXXFLAGS
CXXFLAGS = -std=c++17 -Wall
```

## Best Practices

### Code Organization
- Keep bindings in separate files
- Use namespaces appropriately
- Follow V8 coding conventions
- Maintain consistent formatting

### Memory Management
- Always use HandleScope for V8 objects
- Reset Persistent handles when done
- Clean up libuv resources
- Watch for memory leaks

### Error Handling
- Use TryCatch for V8 operations
- Convert C++ errors to JS exceptions
- Provide meaningful error messages
- Log errors for debugging

---

**Last Updated:** 2025-10-28
**Status:** Complete through Phase 6
