#include <atomic>
extern "C" {
#if 0
}
#endif

const std::memory_order mo_rlx = std::memory_order_relaxed;
const std::memory_order mo_rel = std::memory_order_release;
const std::memory_order mo_acq = std::memory_order_acquire;
const std::memory_order mo_acq_rel = std::memory_order_acq_rel;


int load_store(std::atomic<int> *x, std::atomic<int> *y) {
    int r = y->load(mo_acq);
	x->store(1, mo_rel);
    return r;
}
int load_cas(std::atomic<int> *x, std::atomic<int> *y) {
    int r = y->load(mo_acq);
	x->compare_exchange_weak(r, 20, mo_rel, mo_rlx);
    return r;
}


int main() { return 0; }
}
