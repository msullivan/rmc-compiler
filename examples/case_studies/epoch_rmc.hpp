#ifndef RMC_EPOCH_RMC
#define RMC_EPOCH_RMC

#include <rmc++.h>

namespace rmclib {
    template<class T> using epoch_atomic = rmc::atomic<T>;
}
#include "epoch_shared.hpp"

#endif
