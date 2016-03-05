#ifndef RMC_EPOCH_SC
#define RMC_EPOCH_SC

#include <atomic>

namespace rmclib {
    template<class T> using epoch_atomic = std::atomic<T>;
}
#include "epoch_shared.hpp"

#endif
