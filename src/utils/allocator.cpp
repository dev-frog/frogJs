#include "frogjs/allocator.h"
#include <cstring>
#include <mutex>
#include <vector>

using namespace v8;

namespace frogjs {

namespace {

// Reuses freed blocks by size class. Allocating from V8's default allocator is
// slow when V8 is built with its sandbox, and every Buffer of a few KB and every
// socket read allocates. A reused block is zeroed before it is handed out, unless
// the caller asked for uninitialized memory, which only native code that fills it
// at once does.
class RecyclingAllocator : public ArrayBuffer::Allocator {
public:
    RecyclingAllocator() : inner_(ArrayBuffer::Allocator::NewDefaultAllocator()) {}

    void* Allocate(size_t length) override {
        int size_class = ClassOf(length);
        if (size_class < 0) return inner_->Allocate(length);
        void* block = Take(size_class);
        if (block != nullptr) {
            memset(block, 0, length);
            return block;
        }
        return inner_->Allocate(BlockSize(size_class));
    }

    void* AllocateUninitialized(size_t length) override {
        int size_class = ClassOf(length);
        if (size_class < 0) return inner_->AllocateUninitialized(length);
        void* block = Take(size_class);
        return block != nullptr ? block : inner_->AllocateUninitialized(BlockSize(size_class));
    }

    void Free(void* data, size_t length) override {
        int size_class = ClassOf(length);
        if (size_class < 0) {
            inner_->Free(data, length);
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

private:
    // Classes are powers of two from 512 B to 1 MB. Smaller buffers come from the
    // Buffer pool or V8's own heap, larger ones are rare and expensive anyway.
    static constexpr size_t kMinBlock = 512;
    static constexpr int kClasses = 12;
    static constexpr size_t kMaxCachedBytes = 64 * 1024 * 1024;

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

ArrayBuffer::Allocator* ArrayBufferAllocator() {
    static RecyclingAllocator* allocator = new RecyclingAllocator;
    return allocator;
}

}  // namespace frogjs
