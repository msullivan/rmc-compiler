#ifndef RMC_ATOMIC_H
#define RMC_ATOMIC_H

/**** Linux style atomic primitives, adapted from linux header files. ****/
/**** release/acquire/consume are like what C++ does */

/* Keep the compiler honest */
#define ACCESS_ONCE(x) (*(volatile __typeof__(x) *)&(x))

#define nop() do { } while(0)

#define barrier() __asm__ __volatile__("":::"memory")

#define launder_value(v) \
    ({                                           \
    __typeof__(v) __v = v;                       \
    __asm__ __volatile__("" : "+r" (__v)::);     \
    __v;                                         \
    })

#if defined(__i386) || defined(__x86_64__)

#define x86_lfence() __asm__ __volatile__("lfence":::"memory")
#define x86_sfence() __asm__ __volatile__("sfence" ::: "memory")

#define smp_mb() __asm__ __volatile__("mfence":::"memory")
#define smp_rmb() barrier()
#define smp_wmb() barrier()
#define smp_read_barrier_depends() nop()

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

// On x86 we don't need this
#define ctrl_isync(x) barrier()
#define isync() barrier()
#define vis_barrier() barrier()

#define bogus_dep(v, bs) \
    ({                                           \
    __typeof__(v) __v = v;                       \
    __asm__ __volatile__("" : [val] "+r" (__v) : [bs] "r" (bs):);   \
    __v;                                         \
    })

#define smp_read_barrier_depends() nop()

#elif defined(__arm__)

#define smp_mb() __asm__ __volatile__("dmb ish":::"memory")
#define smp_rmb smp_mb
#define smp_wmb smp_mb
#define smp_read_barrier_depends() nop()

// Make sure you are doing a ctrl yourself!
#define isync() __asm__ __volatile__("isb":::"memory")

#define ctrl_isync(v)                                   \
    __asm__ __volatile__("cmp %[val], %[val];"          \
                         "beq 1f;"                      \
                         "1: isb":  :                   \
                         [val] "r" (v) : "memory","cc")

// In the one simple test I tried, using ctrl_isync didn't seem any better
#define USE_CTRL_ACQUIRE 1

// asdf really this should use LDA/STL on armv8
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

// On arm we produce a zero that is data dependent on an
// argument by xoring it with itself.
#define dependent_zero(v)                                               \
    ({                                                                  \
    __typeof__(v) __i = v;                                              \
    __asm__ __volatile__("eor %[val], %[val];" : [val] "+r" (__i) ::);  \
    (uintptr_t)__i;                                                     \
    })

#define bogus_dep(v, bs) ((v)+dependent_zero(bs))

#define vis_barrier() smp_mb()

#elif defined(__aarch64__)

#define dmb(typ) __asm__ __volatile__("dmb " #typ :::"memory")

#define smp_mb() dmb(ish)
#define smp_rmb() dmb(ishld)
#define smp_wmb() dmb(ishst)
#define smp_read_barrier_depends() nop()

// Make sure you are doing a ctrl yourself!
#define isync() __asm__ __volatile__("isb":::"memory")

#define ctrl_isync(v)                                   \
    __asm__ __volatile__("cmp %[val], %[val];"          \
                         "beq 1f;"                      \
                         "1: isb":  :                   \
                         [val] "r" (v) : "memory","cc")

// In the one simple test I tried, using ctrl_isync didn't seem any better
#define USE_CTRL_ACQUIRE 1

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

// On arm we produce a zero that is data dependent on an
// argument by xoring it with itself.
#define dependent_zero(v)                                               \
    ({                                                                  \
    __typeof__(v) __i = v;                                              \
    __asm__ __volatile__("eor %[val], %[val], %[val];" : [val] "+r" (__i) ::);\
    (uintptr_t)__i;                                                     \
    })

#define bogus_dep(v, bs) ((v)+dependent_zero(bs))

#define vis_barrier() smp_mb()

#elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC__)

// Partial implementation to get full suite to build
#define power_sync(typ) __asm__ __volatile__("sync" :::"memory")
#define power_lwsync(typ) __asm__ __volatile__("lwsync" :::"memory")

#define smp_mb() power_sync()
#define smp_rmb() power_lwsync()
#define smp_wmb() power_lwsync()
#define smp_read_barrier_depends() nop()
#define vis_barrier() power_lwsync()

#define dependent_zero(v)                                               \
    ({                                                                  \
    __typeof__(v) __i = v;                                              \
    uintptr_t __o;                                                      \
    __asm__ __volatile__("xor %[out], %[val], %[val];"                  \
                         : [out] "=r" (__o)                             \
                         : [val] "r" (__i) :);                          \
    __o;                                                                \
    })

#define bogus_dep(v, bs) ((v)+dependent_zero(bs))

// Make sure you are doing your own control!
#define isync() __asm__ __volatile__("isync":::"memory")

#define ctrl_isync(v)                                   \
  __asm__ __volatile__("cmpw 7, %[val], %[val];"         \
                       "bne- 7, 1f;"                        \
                       "1: isync":  :                       \
                       [val] "r" (v) : "memory","cr7")

#define smp_store_release(p, v)                  \
    do {                                         \
        power_lwsync();                          \
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
/* Just emit a lwsync for it */
#define smp_load_acquire(p)                      \
    ({                                           \
    __typeof__(*p) __v = ACCESS_ONCE(*p);        \
    power_lwsync();                              \
    __v;                                         \
    })
#endif


#else
#error CPU not supported
#endif

#define smp_load_consume(p)                      \
    ({                                           \
    __typeof__(*p) __v = ACCESS_ONCE(*p);        \
    smp_read_barrier_depends();                  \
    __v;                                         \
    })


/* Given values v and bs, produce a copy of v that is technically
 * data dependent on bs.
 * v can be a pointer or an integer, bs should be an integer.
 */
#define PADDED __attribute__ ((aligned (128)))

#endif
