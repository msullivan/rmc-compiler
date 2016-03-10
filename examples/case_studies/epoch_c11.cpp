#include <atomic>
#include <utility>
#include <functional>
#include <memory>
#include "epoch_c11.hpp"
#include "remote_fence.hpp"
// Very very closely modeled after crossbeam by aturon.

namespace rmclib {
/////////////////////////////

const std::memory_order mo_rlx = std::memory_order_relaxed;
const std::memory_order mo_rel = std::memory_order_release;
//const std::memory_order mo_acq = std::memory_order_acquire;
const std::memory_order mo_sc  = std::memory_order_seq_cst;

const int kNumEpochs = 3;

thread_local LocalEpoch Epoch::local_epoch_;
std::atomic<Participant::Ptr> Participants::head_;


// XXX: Should this be in a class??
static std::atomic<uintptr_t> global_epoch_{0};
static ConcurrentBag global_garbage_[kNumEpochs];



Participant *Participants::enroll() {
    Participant *p = new Participant();

    Participant::Ptr head = head_;
    for (;;) {
        p->next_ = head;
        if (head_.compare_exchange_weak(head, p)) break;
    }

    return p;
}

/////// Participant is where most of the interesting stuff happens
bool Participant::quickEnter() noexcept {
    assert(!next_.load().tag());

    uintptr_t new_count = in_critical_.load(mo_rlx) + 1;
    in_critical_.store(new_count, mo_rlx);
    // Nothing to do if we were already in a critical section
    if (new_count > 1) return false;

    remote_thread_fence::placeholder(mo_sc);

    // Copy the global epoch to the local one
    uintptr_t global_epoch = global_epoch_.load(mo_rlx);
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
    uintptr_t new_count = in_critical_.load(mo_rlx) - 1;
    // XXX: this may not need to be release
    // in our current setup, nothing can get freed until we do another
    // enter.
    // wait, does anything about the claim make any sense??
    in_critical_.store(new_count, mo_rel);
}

bool Participant::tryCollect() {
    uintptr_t cur_epoch = global_epoch_;

    remote_thread_fence::trigger();

    // Check whether all active threads are in the current epoch so we
    // can advance it.
    // As we do it, we lazily clean up exited threads.
    //
    // XXX: Do we want to factor out the list traversal?
try_again:
    std::atomic<Participant::Ptr> *prevp = &Participants::head_;
    Participant::Ptr cur = *prevp;
    while (cur) {
        Participant::Ptr next = cur->next_;
        if (next.tag()) {
            // This node has exited. Try to unlink it from the
            // list. This will fail if it's already been unlinked or
            // the previous node has exited; in those cases, we start
            // back over at the head of the list.
            if (prevp->compare_exchange_strong(cur, Ptr(next, 0))) {
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
