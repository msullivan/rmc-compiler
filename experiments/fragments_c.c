#include <stdatomic.h>
#include <limits.h>
#include <stdint.h>

#if ULONG_MAX == 4294967295
typedef uint64_t uintdptr_t;
#else
typedef __uint128_t uintdptr_t;
#endif

typedef _Atomic(uintdptr_t) atomic_uintdptr_t;

uintdptr_t load_uintdptr_t(atomic_uintdptr_t *x) {
    return atomic_load_explicit(x, memory_order_relaxed);
}
uintdptr_t cas_uintdptr_t(atomic_uintdptr_t *x,
                          uintdptr_t ev, uintdptr_t nv) {
    atomic_compare_exchange_strong(x, &ev, nv);
    return ev;
}

int main() { return 0; }
