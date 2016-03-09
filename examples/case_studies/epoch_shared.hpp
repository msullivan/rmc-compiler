#ifndef RMC_EPOCH_SHARED
#define RMC_EPOCH_SHARED

#include <utility>
#include <functional>
#include <vector>
#include "util.hpp"
#include "tagged_ptr.hpp"

// Very very closely modeled after crossbeam by aturon.

// All of the externally facing stuff is the same among most of our
// epoch implementations, so we have one shared header for it that is
// parameterized over a definition of "epoch_atomic" that varies
// between implementations.

namespace rmclib {
#if 0
} // f. this
#endif
/////////////////////////////

template<class T> using lf_ptr = T*;

////////////////////////////

//// On my arm test machine, std::function fails to do a small object
//// optimization for functions with one upvar. This had pretty heavy
//// performance impact, so we handroll the thing.
class GarbageCleanup {
private:
    typedef void (*Func)(void *);
    Func func_;
    void *data_;
public:
    GarbageCleanup(Func func, void *data) : func_(func), data_(data) {}
    void operator()() { func_(data_); }
};

class RealLocalGarbage {
    using Bag = std::vector<GarbageCleanup>;

private:
    // Garbage at least one epoch behind current local epoch
    Bag old_;
    // Garbage added in current local epoch or earlier
    Bag cur_;
    // Garbage added in current global epoch
    Bag new_;

    const int kGarbageThreshold = 1024;

public:
    uintptr_t size() {
        return old_.size() + cur_.size() + new_.size();
    };
    // Should we trigger a GC?
    // XXX: Garbage collection is only triggered based on local
    // considerations.  If every thread exits before it tries to GC,
    // we will be sad.
    bool needsCollect() {
        return new_.size() > kGarbageThreshold;
    }
    static void collectBag(Bag &bag);
    void collect();
    void registerCleanup(GarbageCleanup f) {
        // XXX: This has a "terminal irony" (allocating memory to free
        // it), but whatever, it is userspace.
        // But maybe the actual problem is it might need to go to the
        // kernel for an allocation?
        // Avoiding this requires much more seriously constraining what sort
        // of cleanups we can do, though...
        new_.push_back(f);
    }
    // Migrate garbage to the global garbage bags.
    void migrateGarbage();
};

// A variant local garbage that doesn't actually store local garbage
// but instead immediately moves it to global garbage.
class DummyLocalGarbage {
private:
    const int kOpsThreshold = 64;
    int opsCount_{0};
public:
    uintptr_t size() { return 0; }
    // Should we trigger a GC?
    // XXX: Garbage collection is only triggered based on local
    // considerations.  If every thread exits before it tries to GC,
    // we will be sad.
    bool needsCollect() {
        return ++opsCount_ > kOpsThreshold;
    }
    void collect() { opsCount_ = 0; }
    void registerCleanup(GarbageCleanup f);
    void migrateGarbage() { }
};

#if EPOCH_GLOBAL_GARBAGE
using LocalGarbage = DummyLocalGarbage;
#else
using LocalGarbage = RealLocalGarbage;
#endif

// A concurrent garbage bag using a variant of Treiber's stack
class ConcurrentBag {
private:
    struct Node {
        epoch_atomic<Node *> next_{nullptr};
        std::function<void()> cleanup_;
        Node(std::function<void()> cleanup) : cleanup_(std::move(cleanup)) {}
    };

    epoch_atomic<Node *> head_{nullptr};
public:
    void collect();
    void registerCleanup(std::function<void()> f);
};

class Participant {
public:
    using Ptr = tagged_ptr<Participant *>;

    friend class Participants;
    friend class LocalEpoch;
private:
    // Local epoch
    alignas(kCacheLinePadding)
    epoch_atomic<uintptr_t> epoch_{0};
    // Nested critical section count.
    epoch_atomic<uintptr_t> in_critical_{0};

    // Has this thread exited?
    alignas(kCacheLinePadding)
    epoch_atomic<bool> exited_{false};
    // Next pointer in the list of threads
    epoch_atomic<Ptr> next_{nullptr};

    // Collection of garbage
    alignas(kCacheLinePadding)
    LocalGarbage garbage_;

public:
    bool quickEnter() noexcept;
    void enter() noexcept;
    void exit() noexcept;
    bool tryCollect();

    void shutdown() noexcept;

    void registerCleanup(GarbageCleanup f) {
        garbage_.registerCleanup(f);
    }

};

// XXX: Is this (static) the right way to do this?
// Should it just be merged with Participant?? Or made globals??
class Participants {
private:
    friend class Participant;
    static epoch_atomic<Participant::Ptr> head_;

public:
    static Participant *enroll();
};


class LocalEpoch {
private:
    Participant *me_;
public:
    LocalEpoch() : me_(Participants::enroll()) {}
    ~LocalEpoch() {
        me_->enter();
        me_->garbage_.migrateGarbage();
        me_->exit();
        me_->shutdown();
    }
    lf_ptr<Participant> get() { return me_; }
};

// Guard manages the lifecycle of an Epoch critical section.
// To ensure that nodes are only freed in a critical section,
// unlinked is a member of Guard.
class Guard {
private:
    template <typename T>
    static void delete_ptr(void *p) {
        delete reinterpret_cast<T *>(p);
    }

    // Cache the participant so that we don't need to do TLS
    // initialization checks
    Participant *participant_;
public:
    struct quick_enter_t {};
    static constexpr quick_enter_t quick_enter {};

    explicit Guard(Participant *participant) : participant_(participant) {
        participant_->enter();
    }
    Guard(Participant *participant, quick_enter_t)
        : participant_(participant) {
        participant_->quickEnter();
    }
    ~Guard() {
        participant_->exit();
    }

    // Register a cleanup function to be called on the next GC
    // template <typename F>
    // static void registerCleanup(F f) {
    void registerCleanup(GarbageCleanup f) {
        participant_->registerCleanup(f);
    }

    // Register a pointer to be deleted on the next gc
    template <typename T>
    void unlinked(T *p) {
        registerCleanup(GarbageCleanup(delete_ptr<T>,
                                       reinterpret_cast<void *>(p)));
    }

    bool tryCollect() { return participant_->tryCollect(); }
};

class Epoch {
private:
    static thread_local LocalEpoch local_epoch_;
public:
    // The standard entry point for using epochs to reclaim memory.
    static Guard pin() {
        return Guard(local_epoch_.get());
    }

    // RCU style entry points
    // rcuPin(): like pin() but does not ever collect
    static Guard rcuPin() {
        return Guard(local_epoch_.get(), Guard::quick_enter);
    }

    // rcuSynchronize(): wait until all in-progress critical sections
    // terminate
    static void rcuSynchronize() {
        int successes = 0;
        // Three succesful collections means any in-progress reader
        // section must have completed.
        // XXX: does 2 work?
        while (successes < 3) {
            auto guard = pin();
            if (guard.tryCollect()) {
                successes++;
            }
        }
    }

};

//////

}

#endif
