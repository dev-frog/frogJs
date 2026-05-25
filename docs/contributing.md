# Contributing to FrogJS

Thank you for your interest in contributing to FrogJS! This document provides guidelines and instructions for contributing to the project.

## Table of Contents
- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Coding Standards](#coding-standards)
- [Testing Guidelines](#testing-guidelines)
- [Documentation Standards](#documentation-standards)
- [Pull Request Process](#pull-request-process)

## Code of Conduct

- Be respectful and inclusive
- Provide constructive feedback
- Focus on what is best for the community
- Show empathy towards other community members

## Getting Started

### Prerequisites

Before contributing, ensure you have:
- C++17 compatible compiler (GCC, Clang, or MSVC)
- V8 JavaScript Engine installed
- libuv library installed
- Git for version control
- Basic understanding of V8 and libuv APIs

### Setting Up Development Environment

1. **Fork and clone the repository:**
   ```bash
   git clone https://github.com/your-username/frogjs.git
   cd frogjs
   ```

2. **Install dependencies:**
   ```bash
   # macOS
   brew install v8 libuv

   # Linux (Ubuntu/Debian)
   sudo apt-get install libuv1-dev build-essential
   # V8 requires manual installation or custom package
   ```

3. **Build the project:**
   ```bash
   make clean
   make
   ```

4. **Test your build:**
   ```bash
   ./build/frogjs examples/hello.js
   ```

## Development Workflow

### 1. Choose an Issue
- Check [Issues](https://github.com/your-username/frogjs/issues) for open tasks
- Look for tags like `good first issue` for beginners
- Comment on the issue to claim it

### 2. Create a Branch
```bash
git checkout -b feature/your-feature-name
# or
git checkout -b fix/your-bug-fix
```

### 3. Make Your Changes
- Write clean, well-documented code
- Follow coding standards (see below)
- Test your changes thoroughly
- Update documentation if needed

### 4. Test Your Changes
```bash
# Build
make clean && make

# Test with examples
./build/frogjs examples/hello.js
./build/frogjs examples/timers.js
./build/frogjs examples/tcp-server.js
```

### 5. Commit Your Changes
```bash
git add .
git commit -m "feat: add TCP client support"
```

### 6. Push and Create Pull Request
```bash
git push origin feature/your-feature-name
# Then create PR on GitHub
```

## Coding Standards

### C++ Code Style

#### Naming Conventions
- **Classes**: `PascalCase` (e.g., `ConnectionData`)
- **Functions**: `PascalCase` (e.g., `CreateServer`)
- **Variables**: `camelCase` (e.g., `serverData`)
- **Constants**: `UPPER_SNAKE_CASE` (e.g., `MAX_BUFFER_SIZE`)
- **Private members**: trailing underscore (e.g., `privateVar_`)

#### File Organization
```cpp
// 1. Includes
#include <v8.h>
#include <uv.h>
#include <string>

// 2. Using declarations
using namespace v8;

// 3. Structs/Classes
struct MyData {
    // members
};

// 4. Helper functions
std::string HelperFunction() {
    // implementation
}

// 5. Callback functions
void Callback(uv_handle_t* handle) {
    // implementation
}

// 6. Binding functions
void BindingFunction(const FunctionCallbackInfo<Value>& args) {
    // implementation
}

// 7. Setup functions
void SetupModule(Isolate* isolate, Local<Context> context) {
    // implementation
}
```

#### Memory Management
```cpp
// Always use HandleScope for V8 objects
void MyFunction(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    // Local handles are managed automatically
    Local<String> str = String::NewFromUtf8(isolate, "text").ToLocalChecked();

    // Persistent handles must be explicitly reset
    Persistent<Function> callback;
    callback.Reset(isolate, func);
    // ... use callback ...
    callback.Reset(); // Cleanup
}
```

#### Error Handling
```cpp
// Always use TryCatch for V8 operations
TryCatch try_catch(isolate);
Local<Script> script;
if (!Script::Compile(context, source).ToLocal(&script)) {
    String::Utf8Value error(isolate, try_catch.Exception());
    fprintf(stderr, "Error: %s\n", *error);
    return;
}

// Convert C++ errors to JS exceptions
if (error_condition) {
    isolate->ThrowException(
        String::NewFromUtf8(isolate, "Error message").ToLocalChecked()
    );
    return;
}
```

### JavaScript Code Style

#### Naming Conventions
- **Variables/Functions**: `camelCase`
- **Classes**: `PascalCase`
- **Constants**: `UPPER_SNAKE_CASE`
- **Private members**: leading underscore (e.g., `_privateVar`)

#### Module Exports
```javascript
// Prefer named exports
module.exports = {
    myFunction,
    myClass
};

// Single export
module.exports = MyClass;
```

## Testing Guidelines

### Unit Tests (Future)
- Write tests for new functionality
- Use descriptive test names
- Test both success and failure cases
- Mock external dependencies when needed

### Integration Tests
- Test complete workflows
- Use example scripts for integration testing
- Test error conditions
- Verify memory management (no leaks)

### Performance Tests
- Benchmark new features
- Compare against baseline
- Profile memory usage
- Test under load

## Documentation Standards

### Code Comments
```cpp
/**
 * Brief description of function
 *
 * Detailed description if needed
 *
 * @param args V8 function arguments
 * @return Description of return value
 */
void MyFunction(const FunctionCallbackInfo<Value>& args) {
    // Implementation
}
```

### API Documentation
- Update `docs/api-reference.md` for new APIs
- Include examples
- Document parameters and return values
- Note any breaking changes

### Architecture Documentation
- Update `docs/architecture.md` for architectural changes
- Include diagrams for complex systems
- Explain design decisions
- Document trade-offs

### README Updates
- Update README.md for user-visible changes
- Update FEATURES.md for new features
- Keep installation instructions current
- Update examples directory

## Pull Request Process

### PR Title Format
Use conventional commit format:
- `feat:` - New feature
- `fix:` - Bug fix
- `docs:` - Documentation changes
- `refactor:` - Code refactoring
- `test:` - Adding or updating tests
- `chore:` - Maintenance tasks

### PR Description
Include:
- **Summary**: What changes were made and why
- **Testing**: How you tested your changes
- **Breaking Changes**: Any breaking changes (if applicable)
- **Documentation**: Links to updated documentation

### PR Review Process
1. Automated checks must pass
2. At least one maintainer approval required
3. Address review comments
4. Squash commits if requested
5. Maintain approval after changes

### Merge Criteria
- Code follows coding standards
- Tests pass (when tests are implemented)
- Documentation is updated
- No regressions in existing functionality
- At least one maintainer approval

## Development Areas

### High Priority Areas
1. **Error Handling**: Improve error messages and recovery
2. **Memory Management**: Fix memory leaks in module cache
3. **Testing**: Implement test suite
4. **Documentation**: Complete API reference

### Medium Priority Areas
1. **Features**: Add process object, Buffer class
2. **File System**: Add more FS operations
3. **HTTP Module**: Implement HTTP on top of TCP
4. **Build System**: Improve CMake support

### Low Priority Areas
1. **REPL Mode**: Interactive JavaScript shell
2. **Worker Threads**: Multi-threaded JavaScript
3. **ES Modules**: Native import/export support
4. **TypeScript**: Type definitions

## Questions and Support

- **GitHub Issues**: For bug reports and feature requests
- **Discussions**: For questions and community discussion
- **Documentation**: Check existing docs first

## Recognition

Contributors will be:
- Listed in CONTRIBUTORS.md
- Mentioned in release notes
- Credited in significant features

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

---

Thank you for contributing to FrogJS! Every contribution helps make the project better.