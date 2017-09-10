// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_MS_QUEUE_SC
#define RMC_MS_QUEUE_SC

#include <atomic>
#include <utility>
#include "util.hpp"

namespace rmclib {

// I'm doing this all C++ified, but maybe I shouldn't be.
template<typename T,
         unsigned numElems = 1024>
class Ringbuf {
private:
    const unsigned kNumElems = numElems;
    T buf_[numElems];

    static constexpr bool isPowerOf2(size_t v) { return v && ((v&(v-1)) == 0);}
    static_assert(isPowerOf2(numElems), "Ringbuf size must be power of 2");

    alignas(kCacheLinePadding)
    std::atomic<unsigned> front_{0};
    alignas(kCacheLinePadding)
    std::atomic<unsigned> back_{0};

public:
    Ringbuf() {}
    optional<T> dequeue();
    bool enqueue(T t);
};

template<typename T, unsigned numElems>
rmc_noinline
bool Ringbuf<T, numElems>::enqueue(T val) {
    unsigned back = back_;
    unsigned front = front_;

    if (back - kNumElems == front) return false;

    buf_[back % kNumElems] = val;
    back_ = back + 1;
    return true;
}

template<typename T, unsigned numElems>
rmc_noinline
optional<T> Ringbuf<T, numElems>::dequeue() {
    unsigned front = front_;
    unsigned back = back_;

    if (front == back) return optional<T>{};

    T ret = buf_[front % kNumElems];
    front_ = front+1;
    return ret;
}

}


#endif
