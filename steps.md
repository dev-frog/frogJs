# Steps

- 1. Create Directory Structure

  mkdir -p src/core src/bindings src/loop src/utils
  mkdir -p include/frogjs
  mkdir -p lib examples test bin

- 2. Install Dependencies

  > macOS
 `brew install v8 libuv`

  > Verify installations
  `brew list v8`
  `brew list libuv`


- 3. Write Core Runtime (src/core/runtime.cpp)

  This is your main entry point - start with basic V8 initialization and "Hello World"