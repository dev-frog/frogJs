cmake_minimum_required(VERSION 3.10)
project(FrogJS)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find V8 and Libuv
find_package(v8 REQUIRED)
find_package(libuv REQUIRED)

# Include directories
include_directories(${V8_INCLUDE_DIRS} ${LIBUV_INCLUDE_DIRS})

# Add executable
add_executable(frogjs src/core/runtime.cpp)

# Link libraries
target_link_libraries(frogjs ${V8_LIBRARIES} ${LIBUV_LIBRARIES})