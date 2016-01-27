#include <atomic>
#include <utility>
#include "epoch_sc.hpp"
// Very very closely modeled after crossbeam by aturon.

namespace rmclib {
#if 0
} // f. this
#endif
/////////////////////////////

thread_local LocalEpoch Epoch::local_epoch_;
std::atomic<Participant *> Participants::head_;


static std::atomic<uintptr_t> global_epoch_{0};


Participant *Participants::enroll() {
    Participant *p = new Participant();

    Participant *head = head_;
    for (;;) {
        p->next_ = head;
        if (head_.compare_exchange_weak(head, p)) break;
    }

    return p;
}

void Participant::enter() {
    uintptr_t new_count = in_critical_ + 1;
    in_critical_ = new_count;
    // Nothing to do if we were already in a critical section
    if (new_count > 1) return;

    // Copy the global epoch to the local one;
    // if it has changed, garbage collect
    uintptr_t global_epoch = global_epoch_;
    if (global_epoch != epoch_) {
        epoch_ = global_epoch;
        // XXX: TODO: garbage collect
    }

    // XXX: TODO: garbage collect if we are past a threshold

}

void Participant::exit() {
    uintptr_t new_count = in_critical_ - 1;
    in_critical_ = new_count;
}




}
