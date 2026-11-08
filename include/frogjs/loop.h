#pragma once

#include <functional>
#include <utility>
#include <uv.h>
#include <vector>

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

// Native state to release before the thread's loop and isolate go away (open
// handles of unref'd or interrupted work). Bindings register once per thread.
inline thread_local std::vector<std::function<void()>> tls_cleanups;

inline void AddThreadCleanup(std::function<void()> cleanup) {
    tls_cleanups.push_back(std::move(cleanup));
}

inline void RunThreadCleanups() {
    auto cleanups = std::move(tls_cleanups);
    tls_cleanups.clear();
    for (auto& cleanup : cleanups) cleanup();
}

}  // namespace frogjs
