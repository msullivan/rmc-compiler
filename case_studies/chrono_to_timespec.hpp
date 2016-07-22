// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_TIMESPEC_TO_CHRONO
#define RMC_TIMESPEC_TO_CHRONO

#include <chrono>
#include <unistd.h>

namespace rmclib {

template<class Rep, class Period>
static inline struct timespec durationToTimespec(
    const std::chrono::duration<Rep, Period>& rel_time) {

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        rel_time).count();
    timespec ts = { .tv_sec = static_cast<time_t>(ns / 1000000000),
                    .tv_nsec = static_cast<time_t>(ns % 1000000000) };
    return ts;
}

// We only support one clock right now because wtf
template<class Duration>
static inline struct timespec pointToTimeSpec(
    const std::chrono::time_point<
        std::chrono::system_clock, Duration>& timeout) {
        return durationToTimespec(timeout.time_since_epoch());
}

}
#endif
