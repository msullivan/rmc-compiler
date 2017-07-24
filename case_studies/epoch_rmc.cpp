// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include <rmc++.h>
#include <utility>
#include <functional>
#include <memory>
#include "epoch_rmc.hpp"
#include "remote_push.hpp"

// Very very closely modeled after crossbeam by aturon.

namespace rmclib {
/////////////////////////////

const int kNumEpochs = 3;

thread_local LocalEpoch Epoch::local_epoch_;
rmc::atomic<Participant::Ptr> Participant::participants_;

static rmc::atomic<uintptr_t> global_epoch_{0};
static ConcurrentBag global_garbage_[kNumEpochs];

/////// Participant is where most of the interesting stuff happens
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

bool Participant::quickEnter() noexcept {
    uintptr_t new_count = in_critical_ + 1;
    in_critical_ = new_count;
    // Nothing to do if we were already in a critical section
    if (new_count > 1) return false;

    remote_push::placeholder();

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
    VEDGE(pre, exit);
    uintptr_t new_count = in_critical_ - 1;
    L(exit, in_critical_ = new_count);
}

bool Participant::tryCollect() {
    // I think we might not need the load_epoch; we'll be CASing it anyways.
    // Crossbeam makes it SC, though.
    XEDGE(load_epoch, load_head); // XXX: discount double check
    XEDGE(load_head, a);
    XEDGE(a, update_epoch);
    // This could maybe be XEDGE but making it VEDGE lets us make the
    // invariant -vt-> based.
    VEDGE(update_epoch, collect); // XXX: discount double check
    XEDGE(collect, update_local);

    uintptr_t cur_epoch = L(load_epoch, global_epoch_);

    remote_push::trigger();

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
    LS(update_local, epoch_ = new_epoch);

    return true;
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
