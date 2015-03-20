// Copyright (c) 2014-2015 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

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
// This is unhygenic in a nasty way.
// Maybe we should throw some barrier()s in also, to be on the safe side?
#define LS_(name, label, stmt)                                   \
    int XRCAT(______lt, label) = __rmc_action_register(#name);   \
    stmt;                                                        \
    __rmc_action_close(XRCAT(______lt, label));

#define LS(label, stmt) LS_(label, XRCAT(label##_, __COUNTER__), stmt)

#define rmc_push() __rmc_push()

// What orders to use for the atomic ops. Always relaxed in the real version.
#define __rmc_load_order memory_order_relaxed
#define __rmc_store_order memory_order_relaxed
#define __rmc_rmw_order memory_order_relaxed

#else /* !HAS_RMC */
// The compiler doesn't support RMC, so we provide a backup implementation
// based on making all atomic loads/stores acquires/releases.

#define XEDGE(x, y) do { } while (0)
#define VEDGE(x, y) do { } while (0)
#define LS(label, stmt) stmt

// Use __sync_synchronize instead of a C11 SC fence because I felt bad
// relying on details of the implementation of SC fences on our platforms
// (SC fences aren't actually as strong as pushes). Of course, the
// interactions between __sync_synchronize() (which is a "full sync")
// and C11 atomics are totally unspecified, so...
#define rmc_push() ({ __sync_synchronize(); 0; })

// What orders to use for the atomic ops. Always release/acquire
#define __rmc_load_order memory_order_acquire
#define __rmc_store_order memory_order_release
#define __rmc_rmw_order memory_order_acq_rel

#endif /* HAS_RMC */

// To require that the labels in a function be bound "outside" of it,
// we annotate it with "noinline". Inlining would cause us to be
// unable to do this properly, so we want to prevent it. As a hack we
// can then detect whether the function is noinline.
#define RMC_BIND_OUTSIDE_ATTR noinline
#define RMC_BIND_OUTSIDE __attribute__ ((RMC_BIND_OUTSIDE_ATTR))


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

// If NO_REQUIRE_EXPLICIT_ATOMICS is set, then atomic operations can
// be done on any variables, not just _Rmc ones.
#if NO_REQUIRE_EXPLICIT_ATOMICS
#define _Rmc(t) t
#define __rmc_atomic_fixup(e) ((_Atomic(__typeof__(*e))*)(e))
#define RMC_VAR_INIT(value) (value)
#else
// We wrap RMC variables in structs so they have to be used through
// the interface.
#define _Rmc(T)	struct { _Atomic(__typeof__(T)) __val; }
#define __rmc_atomic_fixup(e) (&(e)->__val)
#define RMC_VAR_INIT(value) { .__val = ATOMIC_VAR_INIT(value) }
#endif

// Should have more I suppose
typedef _Rmc(int)               rmc_int;
typedef _Rmc(unsigned int)      rmc_uint;



#define rmc_compare_exchange_strong(object, expected, desired)          \
    atomic_compare_exchange_strong_explicit(                            \
        __rmc_atomic_fixup(object), expected,                           \
        desired, __rmc_rmw_order, __rmc_load_order)
#define rmc_compare_exchange_weak(object, expected, desired)            \
    atomic_compare_exchange_weak_explicit(                              \
        __rmc_atomic_fixup(object), expected,                           \
        desired, __rmc_rmw_order, __rmc_load_order)
#define rmc_exchange(object, desired)                                   \
    atomic_exchange_explicit(                                           \
        __rmc_atomic_fixup(object), desired, __rmc_rmw_order)
#define rmc_fetch_add(object, operand)                               \
    atomic_fetch_add_explicit(                                       \
        __rmc_atomic_fixup(object), operand, __rmc_rmw_order)
#define rmc_fetch_and(object, operand)                               \
    atomic_fetch_and_explicit(                                       \
        __rmc_atomic_fixup(object), operand, __rmc_rmw_order)
#define rmc_fetch_or(object, operand)                                \
    atomic_fetch_or_explicit(                                        \
        __rmc_atomic_fixup(object), operand, __rmc_rmw_order)
#define rmc_fetch_sub(object, operand)                               \
    atomic_fetch_sub_explicit(                                       \
        __rmc_atomic_fixup(object), operand, __rmc_rmw_order)
#define rmc_fetch_xor(object, operand)                               \
    atomic_fetch_xor_explicit(                                       \
        __rmc_atomic_fixup(object), operand, __rmc_rmw_order)

#define rmc_init(object, desired)                               \
    atomic_init(                                                \
        __rmc_atomic_fixup(object), desired)

// rmc_store needs to return a value to be used by L(). We don't
// bother returning a useful one.
#define rmc_store(object, desired)                                   \
    ({atomic_store_explicit(                                         \
        __rmc_atomic_fixup(object), desired, __rmc_store_order);     \
      0;})

#define rmc_load(object)                                             \
    atomic_load_explicit(                                            \
        __rmc_atomic_fixup(object), __rmc_load_order)

#endif
