#include <atomic>
#include <utility>
#include <functional>
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

const int kGarbageThreshold = 20;


Participant *Participants::enroll() {
    Participant *p = new Participant();

    Participant *head = head_;
    for (;;) {
        p->next_ = head;
        if (head_.compare_exchange_weak(head, p)) break;
    }

    return p;
}

/////// Participant is where most of the interesting stuff happens
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
        garbage_.collect();
    }

    if (garbage_.size() > kGarbageThreshold) {
        tryCollect();
    }
}

void Participant::exit() {
    uintptr_t new_count = in_critical_ - 1;
    in_critical_ = new_count;
}

bool Participant::tryCollect() {
    uintptr_t cur_epoch = global_epoch_;

    // XXX: TODO: lazily remove stuff from this list
    for (Participant *p = Participants::head_; p; p = p->next_) {
        // We can only advance the epoch if every thread in a critical
        // section is in the current epoch.
        if (p->in_critical_ && p->epoch_ != cur_epoch) {
            return false;
        }
    }

    // Try to advance the global epoch
    uintptr_t new_epoch = cur_epoch + 1;
    if (!global_epoch_.compare_exchange_strong(cur_epoch, new_epoch)) {
        return false;
    }

    // Update our local epoch and garbage collect
    epoch_ = new_epoch;
    garbage_.collect();

    return true;
}

/////////////
void LocalGarbage::collectBag(Bag &bag) {
    for (auto f : bag) {
        f();
    }
    // XXX: should we shrink capacity?
    bag.clear();
}

void LocalGarbage::collect() {
    collectBag(old_);
    std::swap(old_, cur_);
    std::swap(cur_, new_);
}

}
