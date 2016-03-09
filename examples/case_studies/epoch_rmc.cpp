#include <rmc++.h>
#include <utility>
#include <functional>
#include <memory>
#include "epoch_rmc.hpp"
#include "remote_push.hpp"

// Very very closely modeled after crossbeam by aturon.

namespace rmclib {
#if 0
} // f. this
#endif
/////////////////////////////

const int kNumEpochs = 3;

thread_local LocalEpoch Epoch::local_epoch_;
rmc::atomic<Participant::Ptr> Participants::head_;


// XXX: Should this be in a class??
static rmc::atomic<uintptr_t> global_epoch_{0};
static ConcurrentBag global_garbage_[kNumEpochs];



Participant *Participants::enroll() {
    VEDGE(init_p, cas);

    Participant *p = L(init_p, new Participant());

    Participant::Ptr head = head_;
    for (;;) {
        L(init_p, p->next_ = head);
        if (L(cas, head_.compare_exchange_weak(head, p))) break;
    }

    return p;
}

/////// Participant is where most of the interesting stuff happens
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

    // XXX: TODO: lazily remove stuff from this list
    for (Participant::Ptr p = L(load_head, Participants::head_);
         p; p = L(a, p->next_)) {
        // We can only advance the epoch if every thread in a critical
        // section is in the current epoch.
        if (L(a, p->in_critical_) && L(a, p->epoch_) != cur_epoch) {
            return false;
        }
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
    VEDGE(pre, exit);
    L(exit, exited_ = true);
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

// XXX: I think we could drop the edges here entirely and just rely on

void ConcurrentBag::registerCleanup(std::function<void()> f) {
    // Don't need edge from head_ load because 'push' will also
    // read from the write to head.
    VEDGE(node_setup, push);

    auto *node = L(node_setup, new ConcurrentBag::Node(std::move(f)));

    // Push the node onto a Treiber stack
    for (;;) {
        ConcurrentBag::Node *head = head_;
        L(node_setup, node->next_ = head);
        if (L(push, head_.compare_exchange_weak(head, node))) break;
    }
}

void ConcurrentBag::collect() {
    VEDGE(popall, post);

    // Avoid xchg if empty
    if (!head_) return;

    // Pop the whole stack off
    // Since we only ever unconditionally destroy the whole stack,
    // we don't need to worry about ABA really.
    // (Which for stacks comes up if pop() believes that the address
    // of a node being the same means the next pointer is also...)
    std::unique_ptr<ConcurrentBag::Node> head(
        L(popall, head_.exchange(nullptr)));

    while (head) {
        head->cleanup_();
        head.reset(head->next_);
    }
}

}
