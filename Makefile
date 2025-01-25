# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude

# Libraries
V8_LIBS = -lv8 -lv8_libplatform -lv8_libbase
LIBUV_LIBS = -luv

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