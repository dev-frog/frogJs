#include "frogjs/allocator.h"
#include <cstring>
#include <mutex>
#include <vector>
#ifndef V8_ENABLE_SANDBOX
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace v8;

namespace frogjs {

namespace {

// The largest ArrayBuffer, on every build of V8: 32 GiB, what V8 allows when built
// with its sandbox. Without the sandbox it would accept up to 2^53 bytes and call
// the system allocator with sizes that can't succeed, and a failure there must not
// be able to take the process down.
constexpr size_t kMaxArrayBufferBytes = (size_t{1} << 35) - 1;

// Reuses freed blocks by size class. Allocating from V8's default allocator is
// slow when V8 is built with its sandbox, and every Buffer of a few KB and every
// socket read allocates. A reused block is zeroed before it is handed out, unless
// the caller asked for uninitialized memory, which only native code that fills it
// at once does.
//
// Larger blocks go straight to the system on V8 builds without the sandbox (with
// it, they have to live inside the sandbox). Those builds load PartitionAlloc,
// which replaces malloc and ends the process when it can't serve a request, at
// about 2 GiB and up. mmap() fails by returning an error, which V8 turns into a
// RangeError, so a size that comes from input can't take the process down.
class RecyclingAllocator : public ArrayBuffer::Allocator {
public:
    RecyclingAllocator() : inner_(ArrayBuffer::Allocator::NewDefaultAllocator()) {}

    void* Allocate(size_t length) override {
        int size_class = ClassOf(length);
        if (size_class < 0) return AllocateUnpooled(length, true);
        void* block = Take(size_class);
        if (block != nullptr) {
            memset(block, 0, length);
            return block;
        }
        return inner_->Allocate(BlockSize(size_class));
    }

    void* AllocateUninitialized(size_t length) override {
        int size_class = ClassOf(length);
        if (size_class < 0) return AllocateUnpooled(length, false);
        void* block = Take(size_class);
        return block != nullptr ? block : inner_->AllocateUninitialized(BlockSize(size_class));
    }

    void Free(void* data, size_t length) override {
        int size_class = ClassOf(length);
        if (size_class < 0) {
            FreeUnpooled(data, length);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cached_bytes_ + BlockSize(size_class) <= kMaxCachedBytes) {
                free_[size_class].push_back(data);
                cached_bytes_ += BlockSize(size_class);
                return;
            }
        }
        inner_->Free(data, BlockSize(size_class));
    }

    // V8 refuses larger sizes with a RangeError before calling the allocator
    size_t MaxAllocationSize() const override {
        return kMaxArrayBufferBytes < inner_->MaxAllocationSize() ? kMaxArrayBufferBytes : inner_->MaxAllocationSize();
    }

private:
    // Classes are powers of two from 512 B to 1 MB. Smaller buffers come from the
    // Buffer pool or V8's own heap, larger ones are rare and expensive anyway.
    static constexpr size_t kMinBlock = 512;
    static constexpr int kClasses = 12;
    static constexpr size_t kMaxCachedBytes = 64 * 1024 * 1024;

    void* AllocateUnpooled(size_t length, bool zeroed) {
#ifndef V8_ENABLE_SANDBOX
        if (length > 0) {
            // Fresh anonymous pages are zero-filled, whichever was asked for
            void* block = mmap(nullptr, RoundToPages(length), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            return block == MAP_FAILED ? nullptr : block;
        }
#endif
        return zeroed ? inner_->Allocate(length) : inner_->AllocateUninitialized(length);
    }

    void FreeUnpooled(void* data, size_t length) {
#ifndef V8_ENABLE_SANDBOX
        if (length > 0) {
            munmap(data, RoundToPages(length));
            return;
        }
#endif
        inner_->Free(data, length);
    }

#ifndef V8_ENABLE_SANDBOX
    static size_t RoundToPages(size_t length) {
        const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        return (length + page - 1) / page * page;
    }
#endif

    static size_t BlockSize(int size_class) {
        return kMinBlock << size_class;
    }

    static int ClassOf(size_t length) {
        if (length == 0) return -1;
        for (int i = 0; i < kClasses; i++) {
            if (length <= BlockSize(i)) return i;
        }
        return -1;
    }

    void* Take(int size_class) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<void*>& list = free_[size_class];
        if (list.empty()) return nullptr;
        void* block = list.back();
        list.pop_back();
        cached_bytes_ -= BlockSize(size_class);
        return block;
    }

    ArrayBuffer::Allocator* inner_;
    std::mutex mutex_;  // a buffer can be freed on another thread than it was allocated on
    std::vector<void*> free_[kClasses];
    size_t cached_bytes_ = 0;
};

}  // namespace

size_t MaxBufferLength() {
    return ArrayBufferAllocator()->MaxAllocationSize();
}

ArrayBuffer::Allocator* ArrayBufferAllocator() {
    static RecyclingAllocator* allocator = new RecyclingAllocator;
    return allocator;
}

}  // namespace frogjs
