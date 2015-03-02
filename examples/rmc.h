#ifndef RMC_CORE_H
#define RMC_CORE_H

#ifdef HAS_RMC

/* We signal our labels and edges to our LLVM pass in a fairly hacky
 * way to avoid needing to modify the frontend. Labelled statements
 * are bracketed by calls to the dummy functions __rmc_action_register
 * and __rmc_action_close which indicate the extent of the action and
 * associate it with a name. __rmc_action_close is passed the (bogus)
 * return value from __rmc_action_register in order to make them easy
 * to associate (even if they are duplicated by an optimizer). Edges
 * are specified by calling a dummy function __rmc_edge_register with
 * the names of the labels as arguments.
 *
 * I'm not totally sure how fragile this is at this point. The pass
 * should definitely be run after mem2reg and I suspect that it is
 * actually fairly robust to other optimizations. There certainly are
 * *valid* program transformations that would break things; I'm not
 * sure how likely they are, though, and we can probably detect them
 * and fall back to inserting barriers after all the labels... */


/* C. */
#define RCAT(x,y)      x ## y
#define XRCAT(x,y)     RCAT(x,y)

#ifdef __cplusplus
extern "C" {
#endif

extern int __rmc_action_register(const char *name);
extern int __rmc_action_close(int x);
extern int __rmc_edge_register(int is_vis, const char *src, const char *dst);
extern int __rmc_push(void);

#ifdef __cplusplus
}
#endif

#define RMC_EDGE(t, x, y) __rmc_edge_register(t, #x, #y)
#define XEDGE(x, y) RMC_EDGE(0, x, y)
#define VEDGE(x, y) RMC_EDGE(1, x, y)
/* This is unhygenic in a nasty way. */
/* Maybe we should throw some barrier()s in also, to be on the safe side? */
#define LS_(name, label, stmt)                                   \
    int XRCAT(______lt, label) = __rmc_action_register(#name);   \
    stmt;                                                        \
    __rmc_action_close(XRCAT(______lt, label));

#define LS(label, stmt) LS_(label, XRCAT(label##_, __COUNTER__), stmt)

#define rmc_push() __rmc_push()

#else /* !HAS_RMC */
/* The compiler doesn't support RMC, so we provide a low quality
 * backup implementation.
 * (Probably just as good on x86, to be honest!) */

#include "atomic.h"

#define XEDGE(x, y) do { } while (0)
#define VEDGE(x, y) do { } while (0)
/* Just stick a visibility barrier before and after every label. This
 * isn't good or anything, but it probably works. (Have to do both
 * before and after because of pre/post.) */
/* This is unhygenic in a nasty way. */
#define LS(label, stmt) vis_barrier(); stmt; vis_barrier()

#define rmc_push() ({ smp_mb(); 0; })

#endif /* HAS_RMC */

// Nice way to extract a value directly from a named read
// without needing to manually stick it in a temporary.
#define LE(label, expr) ({LS(label, typeof(expr) _______t = expr); _______t;})
#define LR(label, expr) LE(label, expr)

#ifdef OLD_L_BEHAVIOR
#define L(label, stmt) LS(label, stmt)
#else
#define L(label, stmt) LR(label, stmt)
#endif

#define rmc_push_here_internal(l) do { L(l, rmc_push()); VEDGE(pre, l); XEDGE(l, post); } while (0)
#define rmc_push_here() rmc_push_here_internal(XRCAT(__barrier_push, __COUNTER__))

///////////////////////////////////////////////////////////////////////
// Now define the RMC atomic op instructions. We do this by using the
// C11 atomics at memory_order_relaxed and casting to add _Atomic.
#include "stdatomic.h"

// If REQUIRE_EXPLICIT_ATOMICS is set, then atomic locations need
// to be tagged with _Rmc() in the type and rmc_store/rmc_load should
// be used to use them.
#if REQUIRE_EXPLICIT_ATOMICS
#define _Rmc(t) _Atomic(t)
#define __rmc_atomic_fixup(e) (e)
#else
#define _Rmc(t) t
#define __rmc_atomic_fixup(e) ((_Atomic(__typeof__(*e))*)(e))
#endif

#define rmc_compare_exchange_strong(object, expected, desired)          \
    atomic_compare_exchange_strong_explicit(                            \
        __rmc_atomic_fixup(object), expected,                           \
        desired, memory_order_relaxed, memory_order_relaxed)
#define rmc_compare_exchange_weak(object, expected, desired)            \
    atomic_compare_exchange_weak_explicit(                              \
        __rmc_atomic_fixup(object), expected,                           \
        desired, memory_order_relaxed, memory_order_relaxed)
#define rmc_exchange(object, desired)                                   \
    atomic_exchange_explicit(                                           \
        __rmc_atomic_fixup(object), desired, memory_order_relaxed)
#define rmc_fetch_add(object, operand)                               \
    atomic_fetch_add_explicit(                                       \
        __rmc_atomic_fixup(object), operand, memory_order_relaxed)
#define rmc_fetch_and(object, operand)                               \
    atomic_fetch_and_explicit(                                       \
        __rmc_atomic_fixup(object), operand, memory_order_relaxed)
#define rmc_fetch_or(object, operand)                                \
    atomic_fetch_or_explicit(                                        \
        __rmc_atomic_fixup(object), operand, memory_order_relaxed)
#define rmc_fetch_sub(object, operand)                               \
    atomic_fetch_sub_explicit(                                       \
        __rmc_atomic_fixup(object), operand, memory_order_relaxed)
#define rmc_fetch_xor(object, operand)                               \
    atomic_fetch_xor_explicit(                                       \
        __rmc_atomic_fixup(object), operand, memory_order_relaxed)

// These aren't really necessary, but we might move towards them, and
// I include them for completeness.

// rmc_store needs to return a value to be used by L(). We don't
// bother returning a useful one.
#define rmc_store(object, desired)                                   \
    ({atomic_store_explicit(                                         \
        __rmc_atomic_fixup(object), desired, memory_order_relaxed);  \
      0;})

#define rmc_load(object)                                             \
    atomic_load_explicit(                                            \
        __rmc_atomic_fixup(object), memory_order_relaxed)

#endif
