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

}  // namespace frogjs
