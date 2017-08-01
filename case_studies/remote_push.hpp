// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef REMOTE_PUSH
#define REMOTE_PUSH

#include <atomic>
#include <unistd.h>

#define REMOTE_PUSH_SYS_MEMBARRIER 0
#define REMOTE_PUSH_MPROTECT 0
#define REMOTE_PUSH_SIGNALS 1

namespace rmclib {
namespace remote_push {
inline void compiler_barrier() {
    // clang on arm generates a dmb for an atomic_signal_fence, which
    // kind of defeats the whole fucking purpose, huh?
    //std::atomic_signal_fence(std::memory_order_seq_cst);
    __asm__ __volatile__("":::"memory");
}
}
}


#if REMOTE_PUSH_SYS_MEMBARRIER
// This winds up being disasterously slow.
// I hope Linux goes and adds a faster path than using
// sched_synchronize()

#include <linux/membarrier.h>
#include <sys/syscall.h>

namespace rmclib {
namespace remote_push {

inline int membarrier(int cmd, int flags) {
    return syscall(__NR_membarrier, cmd, flags);
}

inline void placeholder() { compiler_barrier(); }
inline void trigger() {
    membarrier(MEMBARRIER_CMD_SHARED, 0);
}

inline void setup() {}
inline void shutdown() {}

}
}

#elif REMOTE_PUSH_MPROTECT

namespace rmclib {
namespace remote_push {
inline void placeholder() { compiler_barrier(); }

void trigger();
inline void setup() {}
inline void shutdown() {}

}
}

#elif REMOTE_PUSH_SIGNALS

namespace rmclib {
namespace remote_push {
inline void placeholder() { compiler_barrier(); }
void trigger();
void setup();
void shutdown();
}

}

#else
namespace rmclib {
namespace remote_push {

inline void placeholder() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

inline void trigger() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
inline void setup() {}
inline void shutdown() {}

}
}
#endif

// Wrapper with more C++11y names
namespace rmclib {
namespace remote_thread_fence {
// This nominally takes a memory order, but we always just
// force a full fence.
inline void placeholder(std::memory_order order) {
    rmclib::remote_push::placeholder();
}
inline void trigger() { rmclib::remote_push::trigger(); }
inline void setup() { rmclib::remote_push::setup(); }
inline void shutdown() { rmclib::remote_push::shutdown(); }
}
}

#endif
