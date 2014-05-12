#ifndef RMC_ATOMIC_H
#define RMC_ATOMIC_H

/**** Linux style atomic primitives, adapted from linux header files. ****/
/**** release/acquire/consume are like what C++ does */

/* Keep the compiler honest */
#define ACCESS_ONCE(x) (*(volatile __typeof__(x) *)&(x))

/* Optimization barrier */
/* The "volatile" is due to gcc bugs */
#define barrier() __asm__ __volatile__("":::"memory")

#if defined(i386) || defined(__x86_64)

#define smp_mb() __asm__ __volatile__("mfence":::"memory")
#define smp_rmb() __asm__ __volatile__("lfence":::"memory")
#define smp_wmb() __asm__ __volatile__("sfence" ::: "memory")

/* These are for x86 */
#define smp_store_release(p, v)                  \
    do {                                         \
        barrier();                               \
        ACCESS_ONCE(*p) = v;                     \
    } while (0)
#define smp_load_acquire(p)                      \
    ({                                           \
    __typeof__(*p) __v = ACCESS_ONCE(*p);        \
    barrier();                                   \
    __v;                                         \
    })
#define smp_load_consume(p)                      \
    ({                                           \
    __typeof__(*p) __v = ACCESS_ONCE(*p);        \
    barrier();                                   \
    __v;                                         \
    })

// On x86 we don't need this
#define dependent_zero(x) 0

#elif defined(__arm__)

#define smp_mb() __asm__ __volatile__("dmb":::"memory")
#define smp_rmb smp_mb
#define smp_wmb smp_mb

/* These are for ARM */
#define smp_store_release(p, v)                  \
    do {                                         \
        smp_mb();                                \
        ACCESS_ONCE(*p) = v;                     \
    } while (0)
#define smp_load_acquire(p)                      \
    ({                                           \
    __typeof__(*p) __v = ACCESS_ONCE(*p);        \
    smp_mb();                                    \
    __v;                                         \
    })
#define smp_load_consume(p)                      \
    ({                                           \
    __typeof__(*p) __v = ACCESS_ONCE(*p);        \
    barrier();                                   \
    __v;                                         \
    })

// On arm we produce a zero that is data dependent on an
// argument by xoring it with itself.
#define dependent_zero(v)                                   \
    ({                                                      \
    __typeof__(v) __i = v;                                  \
    __asm__ ("eor %[val], %[val];" : [val] "+r" (__i) ::);  \
    __i;                                                    \
    })
#else
#error CPU not supported
#endif

/* Given values v and bs, produce a copy of v that is technically
 * data dependent on bs.
 * v can be a pointer or an integer, bs should be an integer.
 */
#define bullshit_dep(v, bs) ((v)+dependent_zero(bs))

#define PADDED __attribute__ ((aligned (128)))

#endif
