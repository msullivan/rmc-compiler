#include <rmc++.h>
#include "../case_studies/tagged_ptr.hpp"

// We used to fail on this because the __rmc_action_register()s in
// both branches would get merged into one.

using namespace rmclib;
struct Participant {
    using Ptr = tagged_ptr<Participant *>;
    rmc::atomic<uintptr_t> in_critical_{0};
    rmc::atomic<Ptr> next_{nullptr};
};

rmc::atomic<Participant::Ptr> head;

void test(bool b) {
    XEDGE(load_head, a);

    Participant::Ptr cur = L(load_head, head);

    Participant::Ptr next = L(a, cur->next_);
    if (b) {
        L(a, head.compare_exchange_strong(cur, Participant::Ptr(next, 0)));
    } else {
        if (L(a, cur->in_critical_)) return;
    }
}
