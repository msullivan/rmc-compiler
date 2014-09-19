#ifndef RMC_ATOMIC_H
#define RMC_ATOMIC_H

/**** Linux style atomic primitives, adapted from linux header files. ****/
/**** release/acquire/consume are like what C++ does */

/* Keep the compiler honest */
#define ACCESS_ONCE(x) (*(volatile __typeof__(x) *)&(x))

#define nop() do { } while(0)

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
#define ctrl_isync(x) nop()
#define vis_barrier() barrier()

#define bullshit_dep(v, bs) \
    ({                                           \
    __typeof__(v) __v = v;                       \
    __asm__ __volatile__("" : [val] "+r" (__v) : [bs] "r" (bs):);   \
    __v;                                         \
    })


#elif defined(__arm__)

#define smp_mb() __asm__ __volatile__("dmb":::"memory")
#define smp_rmb smp_mb
#define smp_wmb smp_mb

#define ctrl_isync(v)                                   \
    __asm__ __volatile__("cmp %[val], %[val];"          \
                         "beq 1f;"                      \
                         "1: isb":  :                   \
                         [val] "r" (v) : "memory")

// In the one simple test I tried, using ctrl_isync didn't seem any better
#define USE_CTRL_ACQUIRE 1

/* These are for ARM */
#define smp_store_release(p, v)                  \
    do {                                         \
        smp_mb();                                \
        ACCESS_ONCE(*p) = v;                     \
    } while (0)
#if USE_CTRL_ACQUIRE
/* Use ctrlisync to do acquire */
#define smp_load_acquire(p)                      \
    ({                                           \
    __typeof__(*p) __v = ACCESS_ONCE(*p);        \
    ctrl_isync(__v);                             \
    __v;                                         \
    })

#else
/* Just emit a dmb for it */
#define smp_load_acquire(p)                      \
    ({                                           \
    __typeof__(*p) __v = ACCESS_ONCE(*p);        \
    smp_mb();                                    \
    __v;                                         \
    })
#endif

#define smp_load_consume(p)                      \
    ({                                           \
    __typeof__(*p) __v = ACCESS_ONCE(*p);        \
    barrier();                                   \
    __v;                                         \
    })

// On arm we produce a zero that is data dependent on an
// argument by xoring it with itself.
#define dependent_zero(v)                                               \
    ({                                                                  \
    __typeof__(v) __i = v;                                              \
    __asm__ __volatile__("eor %[val], %[val];" : [val] "+r" (__i) ::);  \
    __i;                                                                \
    })

#define bullshit_dep(v, bs) ((v)+dependent_zero(bs))

#define vis_barrier() smp_mb()

#else
#error CPU not supported
#endif

/* Given values v and bs, produce a copy of v that is technically
 * data dependent on bs.
 * v can be a pointer or an integer, bs should be an integer.
 */
#define PADDED __attribute__ ((aligned (128)))

#endif
