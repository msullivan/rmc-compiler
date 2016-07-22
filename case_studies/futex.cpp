// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "futex.hpp"


#if USE_REAL_FUTEX
#else
#include <mutex>
#include <condition_variable>

namespace rmclib {
Futex::WaiterQueue Futex::queues_[Futex::kNumQueues];
}
#endif
