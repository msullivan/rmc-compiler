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

#endif
