// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_EPOCH_C11
#define RMC_EPOCH_C11

#include <atomic>

#define UnsafeTStack EpochGarbageStack
#include "unsafe_tstack_sc.hpp"

namespace rmclib {
    template<class T> using epoch_atomic = std::atomic<T>;
}
#include "epoch_shared.hpp"

#endif
