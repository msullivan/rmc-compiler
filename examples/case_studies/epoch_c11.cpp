#include <atomic>
#include <utility>
#include <functional>
#include <memory>
#include "epoch_c11.hpp"
// Very very closely modeled after crossbeam by aturon.

namespace rmclib {
#if 0
} // f. this
#endif
/////////////////////////////

const std::memory_order mo_rlx = std::memory_order_relaxed;
const std::memory_order mo_rel = std::memory_order_release;
const std::memory_order mo_acq = std::memory_order_acquire;
const std::memory_order mo_sc  = std::memory_order_seq_cst;

const int kNumEpochs = 3;

thread_local LocalEpoch Epoch::local_epoch_;
std::atomic<Participant *> Participants::head_;


// XXX: Should this be in a class??
static std::atomic<uintptr_t> global_epoch_{0};
static ConcurrentBag global_garbage_[kNumEpochs];



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
    uintptr_t new_count = in_critical_.load(mo_rlx) + 1;
    in_critical_.store(new_count, mo_rlx);
    // Nothing to do if we were already in a critical section
    if (new_count > 1) return;

    std::atomic_thread_fence(mo_sc);

    // Copy the global epoch to the local one;
    // if it has changed, garbage collect
    uintptr_t global_epoch = global_epoch_.load(mo_rlx);
    if (global_epoch != epoch_.load(mo_rlx)) {
        epoch_.store(global_epoch, mo_rlx);
        garbage_.collect();
    }

    if (garbage_.needsCollect()) {
        tryCollect();
    }
}

void Participant::exit() {
    uintptr_t new_count = in_critical_.load(mo_rlx) - 1;
    // XXX: this may not need to be release
    // in our current setup, nothing can get freed until we do another
    // enter.
    // wait, does anything about the claim make any sense??
    in_critical_.store(new_count, mo_rel);
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

    // Garbage collect
    global_garbage_[(new_epoch+1) % kNumEpochs].collect();
    garbage_.collect();
    // Now that the collection is done, we can safely update our
    // local epoch.
    epoch_ = new_epoch;


    return true;
}

/////////////
void RealLocalGarbage::collectBag(Bag &bag) {
    for (auto f : bag) {
        f();
    }
    // XXX: should we shrink capacity?
    bag.clear();
}

void RealLocalGarbage::collect() {
    if (size() == 0) return;

    collectBag(old_);
    std::swap(old_, cur_);
    std::swap(cur_, new_);
}

void RealLocalGarbage::migrateGarbage() {
    // We put all three local bags into the current global bag.
    // We could do better than this but why bother, I think.
    auto cleanup = [old = std::move(old_),
                    cur = std::move(cur_),
                    newp = std::move(new_)]() mutable {
        collectBag(old);
        collectBag(cur);
        collectBag(newp);
    };
    global_garbage_[global_epoch_ % kNumEpochs].registerCleanup(cleanup);
}

void DummyLocalGarbage::registerCleanup(std::function<void()> f) {
    global_garbage_[global_epoch_ % kNumEpochs].registerCleanup(f);
}

/////////////
void ConcurrentBag::registerCleanup(std::function<void()> f) {
    auto *node = new ConcurrentBag::Node(std::move(f));

    // Push the node onto a Treiber stack
    for (;;) {
        ConcurrentBag::Node *head = head_;
        node->next_ = head;
        if (head_.compare_exchange_weak(head, node)) break;
    }
}

void ConcurrentBag::collect() {
    // Avoid xchg if empty
    if (!head_) return;

    // Pop the whole stack off
    // Since we only ever unconditionally destroy the whole stack,
    // we don't need to worry about ABA really.
    // (Which for stacks comes up if pop() believes that the address
    // of a node being the same means the next pointer is also...)
    std::unique_ptr<ConcurrentBag::Node> head(head_.exchange(nullptr));

    while (head) {
        head->cleanup_();
        head.reset(head->next_);
    }
}

}
