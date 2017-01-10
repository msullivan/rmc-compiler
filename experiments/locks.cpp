#include "atomic.h"
#include <unistd.h>

// Testing with some different lock implementations.

typedef struct bslock_peterson_t {
    alignas(128)
    int turn;
    alignas(128)
    int flag[2];
} bslock_peterson_t;
#define BSLOCK_INIT { 0, {0, 0} }

// XXX: NOTE THAT THIS IS x86!
// Other things need more shit!
void bslock_lock_peterson(bslock_peterson_t *lock, int me) {
    int other = 1-me;
    ACCESS_ONCE(lock->turn) = other;
    smp_mb();
    ACCESS_ONCE(lock->flag[me]) = 1;
    while (ACCESS_ONCE(lock->flag[other]) && ACCESS_ONCE(lock->turn) == other) {
        continue;
    }
}

void bslock_unlock_peterson(bslock_peterson_t *lock, int me) {
    ACCESS_ONCE(lock->flag[me]) = 0;
}


//
#define bslock_dekker_t bslock_peterson_t

// XXX: NOTE THAT THIS IS x86!
// I am also not super confident here.
void bslock_lock_dekker(bslock_dekker_t *lock, int me) {
    int other = 1-me;
    ACCESS_ONCE(lock->flag[me]) = 1;
    smp_mb();
    while (ACCESS_ONCE(lock->flag[other])) {
        if (ACCESS_ONCE(lock->turn) != me) {
            ACCESS_ONCE(lock->flag[me]) = 0;
            while (ACCESS_ONCE(lock->turn) != me) {
                continue;
            }
            ACCESS_ONCE(lock->flag[me]) = 1;
            smp_mb();
        }
    }
}

void bslock_unlock_dekker(bslock_dekker_t *lock, int me) {
    int other = 1-me;
    ACCESS_ONCE(lock->turn) = other;
    ACCESS_ONCE(lock->flag[me]) = 0;
}

//
#ifdef INCLUDE_CPP
#include <atomic>

typedef struct bslock_dekker11_t {
    std::atomic<int> turn;
    std::atomic<int> flag[2];
} bslock_dekker11_t;

void bslock_lock_dekker11(bslock_dekker11_t *lock, int me) {
    int other = 1-me;
    lock->flag[me] = 1;
    while (lock->flag[other]) {
        if (lock->turn != me) {
            lock->flag[me] = 0;
            while (lock->turn != me) {
                continue;
            }
            lock->flag[me] = 1;
        }
    }
}

void bslock_unlock_dekker11(bslock_dekker11_t *lock, int me) {
    int other = 1-me;
    lock->turn = other;
    lock->flag[me] = 0;
}
#endif



#ifndef NO_TEST
/*************************** Testing ***************************************/
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define INT_TO_PTR(n) ((void *)(long)n)
#define PTR_TO_INT(n) ((int)(long)n)

#ifndef TEST_NAME
#define TEST_NAME peterson
#endif

/* C. */
#define CAT(x,y)      x ## y
#define XCAT(x,y)     CAT(x,y)

#define bslock_lock XCAT(bslock_lock_, TEST_NAME)
#define bslock_unlock XCAT(bslock_unlock_, TEST_NAME)
#define bslock_t XCAT(XCAT(bslock_, TEST_NAME), _t)

#define ITERATIONS 10000000

int PADDED num_in_section;
int PADDED collision_count[2];

bslock_t lock/* = BSLOCK_INIT*/;

long iterations;

void *tester(void *p)
{
    int me = (int)(long)p;
    int i = 0;

    while (i < iterations) {
        bslock_lock(&lock, me);
        if (__sync_fetch_and_add(&num_in_section, 1) == 1) {
            collision_count[me]++;
        }
        __sync_fetch_and_add(&num_in_section, -1);
        bslock_unlock(&lock, me);

        i++;
    }

    return NULL;
}

int main(int argc, char **argv)
{
    iterations = (argc == 2) ? strtol(argv[1], NULL, 0) : ITERATIONS;

    pthread_t thread1, thread2;
    pthread_create(&thread1, NULL, tester, INT_TO_PTR(0));
    pthread_create(&thread2, NULL, tester, INT_TO_PTR(1));

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    printf("collisions: 0 = %d, 1 = %d\n",
           collision_count[0], collision_count[1]);

    return 0;
}

#endif
