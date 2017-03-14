// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_FOUR_SLOT_SC
#define RMC_FOUR_SLOT_SC

#include <atomic>
#include <utility>

namespace rmclib {

// "Four-slot fully asynchronous communication mechanism" by H.R Simpson

// A wait-free system for a single producer to provide a compound
// value to a single consumer.

template<typename T>
class FourSlotSync {
private:
    T data_[2][2];
    std::atomic<int> slot_[2];
    std::atomic<int> latest_;
    std::atomic<int> reading_;

    // writer local state
    int next_;
    T *write_loc_{&data_[1][0]};

public:
    FourSlotSync() {

    }

    void write() {
        slot_[next_] = !slot_[next_];
        latest_ = next_;
        next_ = !reading_;
        int index = !slot_[next_];

        write_loc_ = &data_[next_][index];
    }
    T &write_ref() { return *write_loc_; }

    T &read_ref() {
        int pair = latest_;
        reading_ = pair;
        int index = slot_[pair];
        return data_[pair][index];
    }

    // derivative stuff
    void write(T &&t) {
        write_ref() = std::move(t);
        write();
    }
    void write(const T &t) {
        write_ref() = t;
        write();
    }
    T read() { return read_ref(); }
};

}

#endif
