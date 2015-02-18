#include "rmc.h"

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

/// Ticket taking ones
typedef struct mutex2_t {
    int next;
    int owner;
} mutex2_t;

void mutex2_lock_bad(mutex2_t *lock)
{
    XEDGE(take, check);
    XEDGE(check, post);
    int ticket = L(take, rmc_fetch_add(&lock->next, 1));
    while (ticket != L(check, lock->owner)) {}
}

void mutex2_lock(mutex2_t *lock)
{
    //XEDGE(take, check); // I /think/ maybe we don't need this depending.
    XEDGE(check, loop_out);
    XEDGE(loop_out, post);

    int ticket = L(take, rmc_fetch_add(&lock->next, 1));
    while (ticket != L(check, lock->owner)) {}
    L(loop_out, 0);
}

void mutex2_unlock(mutex2_t *lock)
{
    VEDGE(pre, unlock);
    L(unlock, lock->owner++);
}
