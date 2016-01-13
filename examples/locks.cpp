#include <rmc++.h>

typedef struct mutex_t {
    rmc::atomic<int> locked;
} mutex_t;

void mutex_lock_bad(mutex_t *lock)
{
    XEDGE(trylock, post);
    int expected;
    do {
        expected = 0;
    } while (!L(trylock, lock->locked.compare_exchange_weak(expected, 1)));
}

void mutex_lock(mutex_t *lock)
{
    XEDGE(trylock, loop_out);
    int expected;
    do {
        expected = 0;
    } while (!L(trylock, lock->locked.compare_exchange_weak(expected, 1)));
    LPOST(loop_out);
}

void mutex_unlock(mutex_t *lock)
{
    VEDGE(pre, unlock);
    L(unlock, lock->locked = 0);
}

int nus(rmc::atomic<int> *x) {
    return x->fetch_and(1337);
}


/// Ticket taking ones
typedef struct mutex2_t {
    rmc::atomic<int> next;
    rmc::atomic<int> owner;
} mutex2_t;

void mutex2_lock_bad(mutex2_t *lock)
{
    XEDGE(take, check);
    XEDGE(check, post);
    int ticket = L(take, lock->next.fetch_add(1));
    while (ticket != L(check, lock->owner))
        continue;
}

void mutex2_lock(mutex2_t *lock)
{
    //XEDGE(take, check); // I /think/ maybe we don't need this depending.
    XEDGE(check, loop_out);

    int ticket = L(take, lock->next.fetch_add(1));
    while (ticket != L(check, lock->owner))
        continue;
    LPOST(loop_out);
}

void mutex2_unlock(mutex2_t *lock)
{
    VEDGE(pre, unlock);
    L(unlock, lock->owner = lock->owner + 1);
}
