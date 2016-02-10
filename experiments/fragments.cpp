#include <atomic>
#include <utility>
#include <limits.h>

extern "C" {
#if 0
}
#endif

long long load64(std::atomic<long long> *x) {
	return x->load(std::memory_order_relaxed);
}

void store64(std::atomic<long long> *x, long long y) {
	return x->store(y, std::memory_order_relaxed);
}

struct alignas(2*sizeof(void*)) two_pointer {
    void *p;
    void *q;
};

//typedef std::pair<void *, void *> two_pointer;
two_pointer load_double(std::atomic<two_pointer> *x) {
	return x->load(std::memory_order_relaxed);
}
void store_double(std::atomic<two_pointer> *x,
                         two_pointer y) {
	x->store(y, std::memory_order_relaxed);
}
two_pointer cas_double(std::atomic<two_pointer> *x,
                       two_pointer ev, two_pointer nv) {
    std::atomic_compare_exchange_strong(x, &ev, nv);
    return ev;
}

#if ULONG_MAX == 4294967295
typedef uint64_t uintdptr_t;
#else
typedef __uint128_t uintdptr_t;
#endif

uintdptr_t load_uintdptr_t(std::atomic<uintdptr_t> *x) {
	return x->load(std::memory_order_relaxed);
}
uintdptr_t cas_uintdptr_t(std::atomic<uintdptr_t> *x,
                          uintdptr_t ev, uintdptr_t nv) {
    std::atomic_compare_exchange_strong(x, &ev, nv);
    return ev;
}



int load_acquire(std::atomic<int> *x) {
	return x->load(std::memory_order_acquire);
}

void load_acquire_loop(std::atomic<int> *x) {
	while (x->load(std::memory_order_acquire) == 0);
}

int load_consume(std::atomic<int> *x) {
	return x->load(std::memory_order_consume);
}

void store_release(std::atomic<int> *x) {
	x->store(1, std::memory_order_release);
}

int load_sc(std::atomic<int> *x) {
	return x->load(std::memory_order_seq_cst);
}
void store_sc(std::atomic<int> *x) {
	x->store(1, std::memory_order_seq_cst);
}

int double_sc(std::atomic<int> *x, std::atomic<int> *y) {
	int z = x->load(std::memory_order_seq_cst);
	y->store(1, std::memory_order_seq_cst);
	return z;
}

int load_relaxed(std::atomic<int> *x) {
	return x->load(std::memory_order_relaxed);
}
void store_relaxed(std::atomic<int> *x) {
	x->store(1, std::memory_order_relaxed);
}

int rmw_and_acquire(std::atomic<int> *x) {
    return std::atomic_fetch_and_explicit(x, 1, std::memory_order_acquire);
}
void rmw_and_ignore_res(std::atomic<int> *x) {
    std::atomic_fetch_and_explicit(x, 1, std::memory_order_relaxed);
}


int rmw_acquire(std::atomic<int> *x) {
    return std::atomic_fetch_add_explicit(x, 1, std::memory_order_acquire);
}
int rmw_acq_rel(std::atomic<int> *x) {
    return std::atomic_fetch_add_explicit(x, 1, std::memory_order_acq_rel);
}
int rmw_seq_cst(std::atomic<int> *x) {
    return std::atomic_fetch_add_explicit(x, 1, std::memory_order_seq_cst);
}

int cas_rel__acq(std::atomic<int> *x) {
    int exp = 0;
    std::atomic_compare_exchange_strong_explicit(
        x, &exp, 1, std::memory_order_release, std::memory_order_acquire);
    return exp;
}
int cas_acq_rel__acq(std::atomic<int> *x) {
    int exp = 0;
    std::atomic_compare_exchange_strong_explicit(
        x, &exp, 1, std::memory_order_acq_rel, std::memory_order_acquire);
    return exp;
}
int cas_acq_rel__rlx(std::atomic<int> *x) {
    int exp = 0;
    std::atomic_compare_exchange_strong_explicit(
        x, &exp, 1, std::memory_order_acq_rel, std::memory_order_relaxed);
    return exp;
}
short cas_half(std::atomic<short> *x) {
    short exp = 0;
    x->compare_exchange_strong(
        exp, 1, std::memory_order_acq_rel, std::memory_order_relaxed);
    return exp;
}

int cas_weak(std::atomic<int> *x) {
    int exp = 0;
    std::atomic_compare_exchange_weak_explicit(
        x, &exp, 1, std::memory_order_acq_rel, std::memory_order_acquire);
    return exp;
}

void fence_acquire() {
    std::atomic_thread_fence(std::memory_order_acquire);
}
void fence_release() {
    std::atomic_thread_fence(std::memory_order_release);
}
void fence_sc() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
}



int main() { return 0; }
}
