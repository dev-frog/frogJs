# Compiler and flags
CXX = g++
V8_PATH = /opt/homebrew/Cellar/v8/13.5.212.10
LIBUV_PATH = /opt/homebrew/Cellar/libuv/1.52.1
# V8 aborts at startup if these defines don't match how libv8 was built.
# Homebrew enabled the sandbox up to V8 13.x but disables it from 15.x,
# so detect it from the library's symbols (override with V8_SANDBOX=0/1).
V8_SANDBOX ?= $(shell nm -gU $(V8_PATH)/lib/libv8.dylib 2>/dev/null | c++filt | grep -q 'v8::internal::Sandbox::' && echo 1 || echo 0)
V8_DEFINES = -DV8_COMPRESS_POINTERS
ifeq ($(V8_SANDBOX),1)
V8_DEFINES += -DV8_ENABLE_SANDBOX
endif

CXXFLAGS = -std=c++20 -Wall -Wextra $(V8_DEFINES) -Iinclude -isystem $(V8_PATH)/include -I$(LIBUV_PATH)/include

# make WERROR=1 turns warnings into errors (CI builds this way)
ifeq ($(WERROR),1)
CXXFLAGS += -Werror
endif

# Libraries
V8_LIBS = -L$(V8_PATH)/lib -lv8 -lv8_libplatform
LIBUV_LIBS = -L$(LIBUV_PATH)/lib -luv
# The REPL's Ctrl+C watchdog runs on a thread
ifneq ($(shell uname -s),Darwin)
LIBUV_LIBS += -lpthread
endif

# Source and build directories
SRC_DIR = src
BUILD_DIR = build

# Target executable
TARGET = $(BUILD_DIR)/frogjs

# Source files
SRCS = $(SRC_DIR)/core/runtime.cpp \
       $(SRC_DIR)/core/repl.cpp \
       $(SRC_DIR)/core/line_editor.cpp \
       $(SRC_DIR)/bindings/process.cpp \
       $(SRC_DIR)/bindings/buffer.cpp \
       $(SRC_DIR)/bindings/timers.cpp \
       $(SRC_DIR)/bindings/fs.cpp \
       $(SRC_DIR)/bindings/net.cpp \
       $(SRC_DIR)/bindings/events.cpp \
       $(SRC_DIR)/bindings/stream.cpp \
       $(SRC_DIR)/bindings/http.cpp \
       $(SRC_DIR)/bindings/path.cpp \
       $(SRC_DIR)/bindings/os.cpp \
       $(SRC_DIR)/bindings/modules.cpp \
       $(SRC_DIR)/bindings/util.cpp \
       $(SRC_DIR)/bindings/crypto.cpp \
       $(SRC_DIR)/bindings/child_process.cpp \
       $(SRC_DIR)/bindings/worker_threads.cpp \
       $(SRC_DIR)/utils/js_module.cpp \
       $(SRC_DIR)/utils/errors.cpp \
       $(SRC_DIR)/utils/encoding.cpp \
       $(SRC_DIR)/utils/hash.cpp

# Object files, and the header dependencies the compiler writes next to them
# (-MMD), so changing a header rebuilds what includes it
OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)

# The hash functions are hot loops (PBKDF2 runs them 100k+ times): optimize them
# even though the rest of the build is unoptimized
$(SRC_DIR)/utils/hash.o: CXXFLAGS += -O2

# Subclasses V8's ValueSerializer delegates, and libv8 is built without RTTI
$(SRC_DIR)/bindings/worker_threads.o: CXXFLAGS += -fno-rtti

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(V8_LIBS) $(LIBUV_LIBS)

# Compile source files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(OBJS) $(DEPS)

# Run the executable
run: $(TARGET)
	./$(TARGET)

# ---- Development (see docs/contributing.md) ----

# Self-tests: each exits non-zero if a check fails
TESTS = hello test-buffer test-path test-os test-modules test-events test-streams test-util test-http \
        test-crypto test-child-process test-worker-threads

test: $(TARGET)
	@for t in $(TESTS); do \
		out=$$(./$(TARGET) examples/$$t.js 2>&1) || { echo "$$out"; echo "FAIL examples/$$t.js"; exit 1; }; \
		echo "ok   examples/$$t.js"; \
	done
	@./examples/test-cli.sh ./$(TARGET) | tail -1

# Lint: ESLint for the JavaScript (also the JavaScript embedded in src/), and
# every C++ file compiled with -Werror
lint: lint-js lint-cpp

lint-js: node_modules
	npx eslint .

lint-cpp:
	@for f in $(SRCS); do \
		$(CXX) $(CXXFLAGS) -Werror -fsyntax-only $$f || exit 1; \
	done
	@echo "C++: no warnings"

# clang-format only the C++ lines changed since main (git clang-format), so
# older code isn't reformatted wholesale. FORMAT_BASE picks another base.
FORMAT_BASE ?= main

CLANG_FORMAT_MISSING = { echo "make format needs clang-format and git-clang-format (brew install clang-format)"; exit 1; }

# (git clang-format exits with 1 when it changed something)
format:
	@command -v git-clang-format > /dev/null || $(CLANG_FORMAT_MISSING)
	@git clang-format --force --extensions cpp,h $(FORMAT_BASE); test $$? -le 1

format-check:
	@command -v git-clang-format > /dev/null || $(CLANG_FORMAT_MISSING)
	@out=$$(git clang-format --extensions cpp,h --diff $(FORMAT_BASE)); \
	case "$$out" in \
		""|*"no modified files to format"*|*"did not modify any files"*) echo "C++ format: ok";; \
		*) echo "$$out"; echo "Run 'make format' to fix the formatting above"; exit 1;; \
	esac

# TypeScript declarations: compile types/test, run the result in FrogJS, and
# compare the declarations with the runtime
typecheck: node_modules
	npx tsc -p types/test

check-types: typecheck $(TARGET)
	./$(TARGET) build/types-test/usage.js
	node tools/check-types.js ./$(TARGET)

node_modules: package.json
	npm install
	@touch node_modules

# Everything CI checks
check: lint test check-types

.PHONY: all clean run test lint lint-js lint-cpp format format-check typecheck check-types check