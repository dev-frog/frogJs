# Compiler and flags
CXX = g++
V8_PATH = /opt/homebrew/Cellar/v8/13.5.212.10
LIBUV_PATH = /opt/homebrew/Cellar/libuv/1.51.0
CXXFLAGS = -std=c++20 -Wall -Wextra -DV8_COMPRESS_POINTERS -DV8_ENABLE_SANDBOX -Iinclude -I$(V8_PATH)/include -I$(LIBUV_PATH)/include

# Libraries
V8_LIBS = -L$(V8_PATH)/lib -lv8 -lv8_libplatform
LIBUV_LIBS = -L$(LIBUV_PATH)/lib -luv

# Source and build directories
SRC_DIR = src
BUILD_DIR = build

# Target executable
TARGET = $(BUILD_DIR)/frogjs

# Source files
SRCS = $(SRC_DIR)/core/runtime.cpp

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