// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include <exception>
#include "remote_fence.hpp"

#if REMOTE_PUSH_SYS_MEMBARRIER
#include <assert.h>

namespace rmclib {
namespace remote_push {

static struct Setup {
    Setup() {
        if (membarrier(MEMBARRIER_CMD_QUERY, 0) < 0) {
            assert(0 && "membarrier not supported!");
        }
    }
} dummy;

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


void trigger() {
    dummyPage.forcePush();
}

}
}

#elif REMOTE_PUSH_SIGNALS

// An implementation using signals to force memory barriers.
// Based on "User-Level Implementations of Read-Copy Update" by
// Desnoyers et al.

#include <mutex>
#include <signal.h>
#include <pthread.h>

const int SIGFENCE = SIGUSR2;

// This is super janky but I don't have any other appropriate list
// manipulation code so whatever I will just use the rcu stuff.
// Insert and delete will have some pointless barriers but that really
// is fine.
#define USE_FAKE_CONSUME
#include "rculist_c11.hpp"

namespace rmclib {
namespace remote_push {

struct push_state {
    std::atomic<int> needs_push{0};
    pthread_t tid;
    rculist_node link;
};
static thread_local push_state state;
static std::mutex lock;
static rculist_head threads;

static void smp_mb() { std::atomic_thread_fence(std::memory_order_seq_cst); }

static void sigfence_handler(int n, siginfo_t *info, void *pctx) {
    smp_mb();
    state.needs_push.store(0, std::memory_order_relaxed);
    smp_mb();
}

void trigger() {
    std::unique_lock<std::mutex> lk(lock);
    push_state *thread;
    rculist_for_each_entry(thread, &threads, link) {
        thread->needs_push.store(1, std::memory_order_relaxed);
        smp_mb();
        pthread_kill(thread->tid, SIGFENCE);
    }
    rculist_for_each_entry(thread, &threads, link) {
        while (thread->needs_push.load(std::memory_order_relaxed))
            sched_yield(); // ???
    }
    smp_mb();
}
void setup() {
    std::unique_lock<std::mutex> lk(lock);
    state.tid = pthread_self();
    rculist_insert_tail(&state.link, &threads);
}
void shutdown() {
    smp_mb();
    std::unique_lock<std::mutex> lk(lock);
    rculist_remove(&state.link);

}

static struct Setup {
    Setup() {
        struct sigaction action;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO | SA_NODEFER;
        action.sa_sigaction = sigfence_handler;
        sigaction(SIGFENCE, &action, NULL);
    }
} dummy;

}
}

#endif
