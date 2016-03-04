#ifndef RMC_RCU_SYNC
#define RMC_RCU_SYNC

#include <atomic>
#include "util.hpp"
#include "epoch.hpp"

namespace rmclib {
#if 0
} // f. this
#endif

#ifndef RMC_RCU_SYNCH_GENERAL

// This implementation is less silly but depends on fact that three
// succesful collections works.
// (XXX: would just two work?)
void rcuSynchronize() {
    int successes = 0;
    while (successes < 3) {
        auto guard = Epoch::pin();
        if (guard.tryCollect()) {
            successes++;
        }
    }
}

#else

static void setFlag(void *p) {
    std::atomic<bool> *done = reinterpret_cast<std::atomic<bool> *>(p);
    done->store(true, std::memory_order_release);
}

void rcuSynchronize() {
    std::atomic<bool> done{false};

    {
        auto guard = Epoch::pin();
        guard.registerCleanup(GarbageCleanup(setFlag,
                                             reinterpret_cast<void *>(&done)));
    }

    while (!done.load(std::memory_order_acquire)) {
        auto guard = Epoch::pin();
        guard.tryCollect();
    }
}

#endif

}

#endif
