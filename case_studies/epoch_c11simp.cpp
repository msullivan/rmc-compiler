// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <utility>
#include <functional>
#include <memory>
#include "epoch_c11simp.hpp"
#include "util.hpp"
#include "remote_fence.hpp"

// Very very closely modeled after crossbeam by aturon.

namespace rmclib {
/////////////////////////////

const int kNumEpochs = 3;

thread_local LocalEpoch Epoch::local_epoch_;
std::atomic<Participant::Ptr> Participant::participants_;

static std::atomic<uintptr_t> global_epoch_{0};
static ConcurrentBag global_garbage_[kNumEpochs];

Participant *Participant::enroll() {
    remote_thread_fence::setup();

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
    uintptr_t new_count = in_critical_.load(mo_rlx) + 1;
    in_critical_.store(new_count, mo_rlx);
    // Nothing to do if we were already in a critical section
    if (new_count > 1) return false;

    // Copy the global epoch to the local one
    uintptr_t global_epoch = global_epoch_.load(mo_rlx);
    remote_thread_fence::placeholder(mo_sc);
    epoch_.store(global_epoch, mo_rlx);
    return true;
}

void Participant::enter() noexcept {
    uintptr_t epoch = epoch_.load(mo_rlx);
    if (!quickEnter()) return;

    // If the epoch has changed, garbage collect
    if (epoch != epoch_.load(mo_rlx)) {
        garbage_.collect();
    }

    if (garbage_.needsCollect()) {
        tryCollect();
    }
}

void Participant::exit() noexcept {
    remote_thread_fence::placeholder(mo_sc);
    uintptr_t new_count = in_critical_.load(mo_rlx) - 1;
    in_critical_.store(new_count, mo_rel);
}

bool Participant::tryCollect() {
    remote_thread_fence::trigger();

    uintptr_t cur_epoch = epoch_.load(mo_rlx);

    // Check whether all active threads are in the current epoch so we
    // can advance it.
    // As we do it, we lazily clean up exited threads.
try_again:
    std::atomic<Participant::Ptr> *prevp = &participants_;
    Participant::Ptr cur = prevp->load(mo_acq);
    while (cur) {
        Participant::Ptr next = cur->next_.load(mo_rlx);
        if (next.tag()) {
            // This node has exited. Try to unlink it from the
            // list. This will fail if it's already been unlinked or
            // the previous node has exited; in those cases, we start
            // back over at the head of the list.
            next = Ptr(next, 0); // clear next's tag
            if (prevp->compare_exchange_strong(cur, next, mo_rlx)) {
                Guard g(this);
                g.unlinked(cur.ptr());
            } else {
                goto try_again;
            }
        } else {
            // We can only advance the epoch if every thread in a critical
            // section is in the current epoch.
            if (cur->in_critical_.load(mo_rlx) &&
                cur->epoch_.load(mo_rlx) != cur_epoch) {
                return false;
            }
            prevp = &cur->next_;
        }

        cur = next;
    }

    // Everything visible to the reads from the loop we want hb before
    // the epoch update.
    std::atomic_thread_fence(mo_acq);
    // Try to advance the global epoch
    uintptr_t new_epoch = cur_epoch + 1;
    if (!global_epoch_.compare_exchange_strong(cur_epoch, new_epoch,
                                               mo_acq_rel)) {
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
    Participant::Ptr next = next_.load(mo_acq);
    Participant::Ptr exited_next;
    do {
        exited_next = Participant::Ptr(next, 1);
    } while (!next_.compare_exchange_weak(next, exited_next, mo_acq_rel));

    remote_thread_fence::shutdown();
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
