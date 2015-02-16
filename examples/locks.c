#include <stddef.h>
#include "atomic.h"
#include "rmc.h"

///
// Needs to live somewhere else at some point
// Argh, what do we name these.
#include "stdatomic.h"


#define atomic_fixup(e) ((_Atomic(__typeof__(*e))*)(e))

#define rmc_compare_exchange_strong(object, expected, desired)          \
 atomic_compare_exchange_strong_explicit(atomic_fixup(object), expected, \
                         desired, memory_order_relaxed, memory_order_relaxed)
#define rmc_compare_exchange_weak(object, expected, desired)            \
 atomic_compare_exchange_weak_explicit(atomic_fixup(object), expected, \
                         desired, memory_order_relaxed, memory_order_relaxed)
#define rmc_exchange(object, desired)                                   \
 atomic_exchange_explicit(atomic_fixup(object), desired, memory_order_relaxed)
#define rmc_fetch_add(object, operand)                               \
 atomic_fetch_add_explicit(atomic_fixup(object), operand, memory_order_relaxed)
#define rmc_fetch_and(object, operand)                               \
 atomic_fetch_and_explicit(atomic_fixup(object), operand, memory_order_relaxed)
#define rmc_fetch_or(object, operand)                                \
 atomic_fetch_or_explicit(atomic_fixup(object), operand, memory_order_relaxed)
#define rmc_fetch_sub(object, operand)                               \
 atomic_fetch_sub_explicit(atomic_fixup(object), operand, memory_order_relaxed)
#define rmc_fetch_xor(object, operand)                               \
 atomic_fetch_xor_explicit(atomic_fixup(object), operand, memory_order_relaxed)

///

typedef struct mutex_t {
    int locked;
} mutex_t;

void mutex_lock_bad(mutex_t *lock)
{
    XEDGE(lock, post);
    int expected;
    do {
        expected = 0;
    } while (!L(lock, rmc_compare_exchange_weak(&lock->locked, &expected, 1)));
}

void mutex_lock(mutex_t *lock)
{
    XEDGE(lock, loop_out);
    XEDGE(loop_out, post);
    int expected;
    do {
        expected = 0;
    } while (L(lock, rmc_compare_exchange_weak(&lock->locked, &expected, 1)) == 0);
    L(loop_out, 0);
}

void mutex_unlock(mutex_t *lock)
{
    VEDGE(pre, unlock);
    L(unlock, lock->locked = 0);
}

int nus(int *x) {
    return rmc_fetch_and(x, 1337);
}
