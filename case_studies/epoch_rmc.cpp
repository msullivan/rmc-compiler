// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include <rmc++.h>
#include <utility>
#include <functional>
#include <memory>
#include "epoch_rmc.hpp"

// Very very closely modeled after crossbeam by aturon.

namespace rmclib {
/////////////////////////////

const int kNumEpochs = 3;

thread_local LocalEpoch Epoch::local_epoch_;
rmc::atomic<Participant::Ptr> Participant::participants_;
static ConcurrentBag global_garbage_[kNumEpochs];

/// BEGIN SNIP
static rmc::atomic<uintptr_t> global_epoch_{0};

/////// Participant is where most of the interesting stuff happens
bool Participant::quickEnter() noexcept {
    PEDGE(enter, body);

    uintptr_t new_count = in_critical_ + 1;
    L(enter, in_critical_ = new_count);
    // Nothing to do if we were already in a critical section
    if (new_count > 1) return false;

    // Copy the global epoch to the local one;
    uintptr_t global_epoch = L(enter, global_epoch_);
    epoch_ = global_epoch;
    LPOST(body);
    return true;
}

void Participant::enter() noexcept {
    uintptr_t epoch = epoch_;
    if (!quickEnter()) return;

    // If the epoch has changed, collect our local garbage
    if (epoch != epoch_) {
        garbage_.collect();
    }

    if (garbage_.needsCollect()) {
        tryCollect();
    }
}

void Participant::exit() noexcept {
    // This is actually super unfortunate. quickEnter() will
    // subsequently write to in_critical_, and we need the body of a
    // critical section to be visible to code that reads from that
    // subsequent write as well as this one.
    // In C++11, we would make the write to in_critical_ be a release
    // and the convoluted release sequence rules would save us.
    VEDGE(pre, exit);
    LPOST(exit);
    uintptr_t new_count = in_critical_ - 1;
    L(__exit, in_critical_ = new_count);
}

bool Participant::tryCollect() {
    XEDGE(load_head, a);
    VEDGE(a, update_epoch);
    XEDGE(update_epoch, post);
    VEDGE(collect, update_local);

    uintptr_t cur_epoch = epoch_;

    // Check whether all active threads are in the current epoch so we
    // can advance it.
    // As we do it, we lazily clean up exited threads.
try_again:
    rmc::atomic<Participant::Ptr> *prevp = &participants_;
    Participant::Ptr cur = L(load_head, *prevp);
    while (cur) {
        Participant::Ptr next = L(a, cur->next_);
        if (next.tag()) {
            // This node has exited. Try to unlink it from the
            // list. This will fail if it's already been unlinked or
            // the previous node has exited; in those cases, we start
            // back over at the head of the list.
            next = Ptr(next, 0); // clear next's tag
            if (L(a, prevp->compare_exchange_strong(cur, next))) {
                Guard g(this);
                g.unlinked(cur.ptr());
            } else {
                goto try_again;
            }
        } else {
            // We can only advance the epoch if every thread in a critical
            // section is in the current epoch.
            if (L(a, cur->in_critical_) && L(a, cur->epoch_) != cur_epoch) {
                return false;
            }
            prevp = &cur->next_;
        }

        cur = next;
    }

    // Try to advance the global epoch
    uintptr_t new_epoch = cur_epoch + 1;
    if (!L(update_epoch,
           global_epoch_.compare_exchange_strong(cur_epoch, new_epoch))) {
        return false;
    }

    // Garbage collect
    LS(collect, {
        global_garbage_[(new_epoch+1) % kNumEpochs].collect();
        garbage_.collect();
    });
    // Now that the collection is done, we can safely update our
    // local epoch.
    L(update_local, epoch_ = new_epoch);

    return true;
}

// Participant lifetime management
Participant *Participant::enroll() {
    VEDGE(init_p, cas);

    Participant *p = L(init_p, new Participant());

    Participant::Ptr head = participants_;
    for (;;) {
        L(init_p, p->next_ = head);
        if (L(cas, participants_.compare_exchange_weak(head, p))) break;
    }

    return p;
}
void Participant::shutdown() noexcept {
    VEDGE(before, exit);
    LPRE(before);

    Participant::Ptr next = next_;
    Participant::Ptr exited_next;
    do {
        exited_next = Participant::Ptr(next, 1);
    } while (!L(exit, next_.compare_exchange_weak(next, exited_next)));

}
/// END SNIP

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
