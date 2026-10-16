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

# Libraries
V8_LIBS = -L$(V8_PATH)/lib -lv8 -lv8_libplatform
LIBUV_LIBS = -L$(LIBUV_PATH)/lib -luv

# Source and build directories
SRC_DIR = src
BUILD_DIR = build

# Target executable
TARGET = $(BUILD_DIR)/frogjs

# Source files
SRCS = $(SRC_DIR)/core/runtime.cpp \
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
       $(SRC_DIR)/utils/js_module.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(V8_LIBS) $(LIBUV_LIBS)

# Compile source files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(OBJS)

# Run the executable
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run