#pragma once

#include <cstddef>
#include <v8.h>

namespace frogjs {

// The ArrayBuffer allocator every isolate uses. There is one per process and it
// is never destroyed: a SharedArrayBuffer or transferred buffer can outlive the
// thread that allocated it, and it is freed through its allocator.
v8::ArrayBuffer::Allocator* ArrayBufferAllocator();

// The largest Buffer or ArrayBuffer, in bytes
size_t MaxBufferLength();

// The most native code copies into a std::string at once. malloc can end the
// process on larger requests with V8 builds that replace it, so code that copies
// a caller's buffer has to refuse more than this.
constexpr size_t kMaxNativeCopy = (size_t{1} << 31) - 1;

}  // namespace frogjs
