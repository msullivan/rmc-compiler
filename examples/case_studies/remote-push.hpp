#ifndef REMOTE_PUSH
#define REMOTE_PUSH

#include <atomic>
#include <cstdio>
#include <unistd.h>

#define REMOTE_PUSH_MPROTECT 1

#ifdef REMOTE_PUSH_MPROTECT
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
    std::atomic_signal_fence(std::memory_order_seq_cst);
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


#endif
