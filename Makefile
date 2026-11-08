# Compiler and flags
CXX = g++
# Install prefixes of the dependencies: Homebrew's by default, or pass your own
# (make V8_PATH=/usr/local LIBUV_PATH=/usr)
ifneq ($(shell command -v brew 2>/dev/null),)
V8_PATH ?= $(shell brew --prefix v8)
LIBUV_PATH ?= $(shell brew --prefix libuv)
else
V8_PATH ?= /usr/local
LIBUV_PATH ?= /usr/local
endif
# V8 aborts at startup if these defines don't match how libv8 was built.
# Homebrew enabled the sandbox up to V8 13.x but disables it from 15.x,
# so detect it from the library's symbols (override with V8_SANDBOX=0/1).
V8_SANDBOX ?= $(shell nm -gU $(V8_PATH)/lib/libv8.dylib 2>/dev/null | c++filt | grep -q 'v8::internal::Sandbox::' && echo 1 || echo 0)
V8_DEFINES = -DV8_COMPRESS_POINTERS
ifeq ($(V8_SANDBOX),1)
V8_DEFINES += -DV8_ENABLE_SANDBOX
endif

# Optimized by default; make DEBUG=1 for a debugger-friendly build
ifeq ($(DEBUG),1)
OPTFLAGS ?= -O0 -g
else
OPTFLAGS ?= -O2
endif

# EXTRA_FLAGS: instrumentation for the sanitizer and coverage builds below
CXXFLAGS = -std=c++20 -Wall -Wextra $(OPTFLAGS) $(EXTRA_FLAGS) $(V8_DEFINES) -Iinclude -isystem $(V8_PATH)/include -I$(LIBUV_PATH)/include

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
       $(SRC_DIR)/utils/hash.cpp \
       $(SRC_DIR)/utils/coverage.cpp \
       $(SRC_DIR)/utils/allocator.cpp

# Object files, and the header dependencies the compiler writes next to them
# (-MMD), so changing a header rebuilds what includes it. They live under
# BUILD_DIR, so the instrumented builds don't mix with the normal one.
OBJS = $(SRCS:%.cpp=$(BUILD_DIR)/obj/%.o)
DEPS = $(OBJS:.o=.d)

# These subclass V8 classes, and libv8 is built without RTTI
$(BUILD_DIR)/obj/$(SRC_DIR)/bindings/worker_threads.o: CXXFLAGS += -fno-rtti
$(BUILD_DIR)/obj/$(SRC_DIR)/utils/coverage.o: CXXFLAGS += -fno-rtti
$(BUILD_DIR)/obj/$(SRC_DIR)/utils/allocator.o: CXXFLAGS += -fno-rtti

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(V8_LIBS) $(LIBUV_LIBS)

# Compile source files
$(BUILD_DIR)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(OBJS) $(DEPS)

# Run the executable
run: $(TARGET)
	./$(TARGET)

# ---- Development (see docs/contributing.md) ----

# Self-tests: each exits non-zero if a check fails. TEST_TIMEOUT (seconds, per
# test file) turns a hang into a failure.
TESTS = hello test-buffer test-path test-os test-modules test-events test-streams test-util test-http \
        test-crypto test-child-process test-worker-threads test-fs test-net test-process test-timers \
        test-module-loading test-errors test-http-limits test-edges test-memory
TEST_TIMEOUT ?= 180
WITH_TIMEOUT = perl -e 'alarm shift; exec @ARGV' $(TEST_TIMEOUT)

test: $(TARGET)
	@for t in $(TESTS); do \
		out=$$($(WITH_TIMEOUT) ./$(TARGET) examples/$$t.js 2>&1) || { echo "$$out"; echo "FAIL examples/$$t.js"; exit 1; }; \
		case "$$t" in hello) ;; *) echo "$$out" | tail -1 | grep -q ' 0 failed$$' || { echo "$$out"; echo "FAIL examples/$$t.js did not finish"; exit 1; };; esac; \
		echo "ok   examples/$$t.js"; \
	done
	@./examples/test-cli.sh ./$(TARGET) | tail -1
	@python3 examples/test-repl.py ./$(TARGET) | tail -1

# The tests on a build with AddressSanitizer and UndefinedBehaviorSanitizer
# (LeakSanitizer isn't available on macOS: see leakcheck)
SAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize=vptr
export ASAN_OPTIONS = detect_leaks=0:quarantine_size_mb=8
export UBSAN_OPTIONS = print_stacktrace=1:halt_on_error=1
test-asan:
	$(MAKE) test BUILD_DIR=build/asan OPTFLAGS="-O1 -g" EXTRA_FLAGS="$(SAN_FLAGS)" TEST_TIMEOUT=300

# Native leaks at exit, reported by macOS's leaks tool
leakcheck: $(TARGET)
	./tools/leakcheck.sh ./$(TARGET)

# Line coverage: C++ from llvm-cov, embedded JavaScript from V8's precise
# coverage. COVERAGE_MIN fails the target below that percentage.
COVERAGE_MIN ?= 0
coverage:
	$(MAKE) build/coverage/frogjs BUILD_DIR=build/coverage OPTFLAGS="-O0 -g" \
		EXTRA_FLAGS="-fprofile-instr-generate -fcoverage-mapping -DFROGJS_COVERAGE"
	python3 tools/coverage.py build/coverage/frogjs --min $(COVERAGE_MIN)

# Benchmarks against Node.js
bench: $(TARGET)
	python3 benchmark/run.py ./$(TARGET)

# Random input for the HTTP parser and Buffer, on the sanitizer build
fuzz:
	$(MAKE) build/asan/frogjs BUILD_DIR=build/asan OPTFLAGS="-O1 -g" EXTRA_FLAGS="$(SAN_FLAGS)"
	./build/asan/frogjs tools/fuzz.js

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

# Every declared export has to appear in the API reference
check-docs: node_modules
	node tools/check-docs.js

node_modules: package.json
	npm install
	@touch node_modules

# Everything CI checks
check: lint test check-types check-docs

.PHONY: all clean run test test-asan leakcheck coverage bench fuzz lint lint-js lint-cpp format format-check typecheck check-types check-docs check