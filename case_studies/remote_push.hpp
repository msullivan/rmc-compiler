// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef REMOTE_PUSH
#define REMOTE_PUSH

#include <atomic>
#include <cstdio>
#include <unistd.h>

#define REMOTE_PUSH_SYS_MEMBARRIER 0
#define REMOTE_PUSH_MPROTECT 0

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

static struct Setup {
    Setup() {
        if (membarrier(MEMBARRIER_CMD_QUERY, 0) < 0) {
            assert(0 && "membarrier not supported!");
        }
    }
} dummy;

inline void placeholder() {
    // clang on arm generates a dmb for an atomic_signal_fence, which
    // kind of defeats the whole fucking purpose, huh?
    //std::atomic_signal_fence(std::memory_order_seq_cst);
    __asm__ __volatile__("":::"memory");
}

inline void trigger() {
    membarrier(MEMBARRIER_CMD_SHARED, 0);
}

}
}

#elif REMOTE_PUSH_MPROTECT
// XXX: This might not actually work on ARM!
// ARM has instructions to remotely invalidate TLB entries!

#include <sys/mman.h>

namespace rmclib {
namespace remote_push {

const int kMadeupPageSize = 4096;

static struct DummyPage {
    alignas(kMadeupPageSize)
    char page[kMadeupPageSize];

    DummyPage() {
        /* Lock the memory so it can't get paged out. If it gets paged
         * out, changing its protection won't accomplish anything. */
        if (mlock(page, 1) < 0) std::terminate();
    }

    /* Force a push using a TLB shootdown */
    void forcePush() {
        /* Make the dummy page writable, then take it away.
         * We do this because there is really no need to TLB shootdown
         * when /adding/ permissions. */
        if (mprotect(&this->page, 1, PROT_READ|PROT_WRITE) < 0 ||
            mprotect(&this->page, 1, PROT_READ) < 0) {
            std::terminate();
        }
	}
} dummyPage;

inline void placeholder() {
    // clang on arm generates a dmb for an atomic_signal_fence, which
    // kind of defeats the whole fucking purpose, huh?
    //std::atomic_signal_fence(std::memory_order_seq_cst);
    __asm__ __volatile__("":::"memory");
}

inline void trigger() {
    dummyPage.forcePush();
}


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
}
}

#endif
