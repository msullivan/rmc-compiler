#ifndef RMC_EPOCH_SC
#define RMC_EPOCH_SC

#include <atomic>
#include <utility>
// Very very closely modeled after crossbeam by aturon.

namespace rmclib {
#if 0
} // f. this
#endif
/////////////////////////////

template<class T> using lf_ptr = T*;

////////////////////////////

class LocalGarbage {


};


class Participant {
    friend class Participants;
    friend class LocalEpoch;
private:
    // Local epoch
    std::atomic<uintptr_t> epoch_{0};
    // Nested critical section count.
    std::atomic<uintptr_t> in_critical_{0};
    // Collection of garbage
    LocalGarbage garbage_;
    // Has this thread exited?
    std::atomic<bool> exited_{false};
    // Next pointer in the list of threads
    std::atomic<Participant *> next_;

public:

    void enter();
    void exit();
};

// XXX: Is this (static) the right way to do this?
class Participants {
private:
    static std::atomic<Participant *> head_;

public:
    static Participant *enroll();
};



class LocalEpoch {
private:
    Participant *me_;
public:
    LocalEpoch() : me_(Participants::enroll()) {}
    ~LocalEpoch() {
        // XXX: TODO: don't leak everything...
        me_->exited_ = true;
    }
    lf_ptr<Participant> get() { return me_; }

};


class Guard {
public:
    ~Guard();
};

class Epoch {
private:
    static thread_local LocalEpoch local_epoch_;
public:
    static void enter() { local_epoch_.get()->enter(); }
    static void exit() { local_epoch_.get()->exit(); }
    static Guard pin() {
        enter();
        return Guard();
    }
};
inline Guard::~Guard() { Epoch::exit(); }

//////




}


#endif
