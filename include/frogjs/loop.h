#pragma once

#include <uv.h>

namespace frogjs {

// Each thread running JavaScript has its own event loop: the main thread uses
// the default loop, every worker thread a loop of its own (set when it starts)
inline thread_local uv_loop_t* tls_loop = nullptr;

// The calling thread's event loop
inline uv_loop_t* Loop() {
    return tls_loop != nullptr ? tls_loop : uv_default_loop();
}

// True on a worker_threads Worker's thread
inline bool IsWorkerThread() {
    return tls_loop != nullptr;
}

}  // namespace frogjs
