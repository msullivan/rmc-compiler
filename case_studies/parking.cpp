// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "parking.hpp"

namespace rmclib {

#if USE_FUTEX_PARKING
thread_local Parking::ThreadNode Parking::me_;
#elif USE_PTHREAD_PARKING
thread_local Parking::ThreadNode Parking::me_ = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    false
};
#else
thread_local Parking::ThreadNode Parking::me_;
#endif

}
