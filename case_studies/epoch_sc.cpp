// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <utility>
#include <functional>
#include <memory>
#include "epoch_sc.hpp"
// Very very closely modeled after crossbeam by aturon.

namespace rmclib {

const int kNumEpochs = 3;

thread_local LocalEpoch Epoch::local_epoch_;
std::atomic<Participant::Ptr> Participant::participants_;


// XXX: Should this be in a class??
static std::atomic<uintptr_t> global_epoch_{0};
static ConcurrentBag global_garbage_[kNumEpochs];



Participant *Participant::enroll() {
    Participant *p = new Participant();

    Participant::Ptr head = participants_;
    for (;;) {
        p->next_ = head;
        if (participants_.compare_exchange_weak(head, p)) break;
    }

    return p;
}

/////// Participant is where most of the interesting stuff happens
bool Participant::quickEnter() noexcept {
    uintptr_t new_count = in_critical_ + 1;
    in_critical_ = new_count;
    // Nothing to do if we were already in a critical section
    if (new_count > 1) return false;

    // XXX: Consider whether we do need technically need a fence for
    // when the contents of the critical section are not SC!
    // I think I convinced myself that we don't but need to find those
    // notes...

    // Copy the global epoch to the local one;
    // if it has changed, garbage collect
    uintptr_t global_epoch = global_epoch_;
    epoch_ = global_epoch;
    return true;
}

void Participant::enter() noexcept {
    uintptr_t epoch = epoch_;
    if (!quickEnter()) return;

    // If the epoch has changed, garbage collect
    if (epoch != epoch_) {
        garbage_.collect();
    }

    if (garbage_.needsCollect()) {
        tryCollect();
    }
}

void Participant::exit() noexcept {
    uintptr_t new_count = in_critical_ - 1;
    in_critical_ = new_count;
}

bool Participant::tryCollect() {
    uintptr_t cur_epoch = global_epoch_;

    // Check whether all active threads are in the current epoch so we
    // can advance it.
    // As we do it, we lazily clean up exited threads.
try_again:
    std::atomic<Participant::Ptr> *prevp = &participants_;
    Participant::Ptr cur = *prevp;
    while (cur) {
        Participant::Ptr next = cur->next_;
        if (next.tag()) {
            // This node has exited. Try to unlink it from the
            // list. This will fail if it's already been unlinked or
            // the previous node has exited; in those cases, we start
            // back over at the head of the list.
            next = Ptr(next, 0); // clear next's tag
            if (prevp->compare_exchange_strong(cur, next)) {
                Guard g(this);
                g.unlinked(cur.ptr());
            } else {
                goto try_again;
            }
        } else {
            // We can only advance the epoch if every thread in a critical
            // section is in the current epoch.
            if (cur->in_critical_ && cur->epoch_ != cur_epoch) {
                return false;
            }
            prevp = &cur->next_;
        }

        cur = next;
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

void Participant::shutdown() noexcept {
    Participant::Ptr next = next_;
    Participant::Ptr exited_next;
    do {
        exited_next = Participant::Ptr(next, 1);
    } while (!next_.compare_exchange_weak(next, exited_next));
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

void DummyLocalGarbage::registerCleanup(GarbageCleanup f) {
    global_garbage_[global_epoch_ % kNumEpochs].registerCleanup(f);
}

/////////////
void ConcurrentBag::registerCleanup(ConcurrentBag::Cleanup f) {
    stack_.pushNode(new Node(std::move(f)));
}

void ConcurrentBag::collect() {
    // Pop the whole stack off
    // Since we only ever unconditionally destroy the whole stack,
    // we don't need to worry about ABA really.
    // (Which for stacks comes up if pop() believes that the address
    // of a node being the same means the next pointer is also...)
    std::unique_ptr<ConcurrentBag::Node> head(stack_.popAll());

    while (head) {
        head->data_();
        head.reset(head->next_);
    }
}

}
