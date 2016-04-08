#include "futex.hpp"


#if USE_REAL_FUTEX
#else
#include <mutex>
#include <condition_variable>

namespace rmclib {
Futex::WaiterQueue Futex::queues_[Futex::kNumQueues];
}
#endif
