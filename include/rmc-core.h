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

#define RMC_FORCE_INLINE __attribute__((always_inline))


#ifdef __cplusplus
extern "C" {
#endif

extern int __rmc_action_register(const char *name) __attribute__((noduplicate));
extern int __rmc_action_close(int x) __attribute__((noduplicate));
extern int __rmc_edge_register(int is_vis, const char *src, const char *dst)
  __attribute__((noduplicate));
extern int __rmc_push(void) __attribute__((noduplicate));

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
#define __rmc_push() ({ __sync_synchronize(); 0; })

// What orders to use for the atomic ops.
// Generally use release/acquire, but if RMC_FALLBACK_USE_SC
// is set, use seq_cst instead to help debug whether bugs
// are related to weak memory behavior.
#ifdef RMC_FALLBACK_USE_SC
#define __rmc_load_order memory_order_seq_cst
#define __rmc_store_order memory_order_seq_cst
#define __rmc_rmw_order memory_order_seq_cst
#else /* !RMC_FALLBACK_USE_SC */
// What orders to use for the atomic ops. Always release/acquire
#define __rmc_load_order memory_order_acquire
#define __rmc_store_order memory_order_release
#define __rmc_rmw_order memory_order_acq_rel
#endif /* RMC_FALLBACK_USE_SC */

#endif /* HAS_RMC */

// To require that the labels in a function be bound "outside" of it,
// we annotate it with "noinline". Inlining would cause us to be
// unable to do this properly, so we want to prevent it. As a hack we
// can then detect whether the function is noinline.
#define RMC_BIND_OUTSIDE_ATTR noinline
#define RMC_BIND_OUTSIDE __attribute__ ((RMC_BIND_OUTSIDE_ATTR))


// Nice way to extract a value directly from a named read
// without needing to manually stick it in a temporary.
// __rmc_typeof is defined in the C and C++ support files:
// in C++ we do template magic to turn rmc<T> into T, which will
// force the dereference.
#define L(label, expr)                                         \
  ({LS(label, __rmc_typeof(expr) _______t = expr); _______t;})

// Convenience macros for labels that have relationships with
// all program order successors or predecessors.
// The edge type is always visibility, since it implies execution order.
// Since no-ops are only meaningful for transitivity, the compiler is
// smart enough to do execution-order edge cutting if only execution
// edges are drawn.
#define __rmc_noop() ((void)0)
#define LPRE(label) do { VEDGE(pre, label); LS(label, __rmc_noop()); } while(0)
#define LPOST(label) do { VEDGE(label, post); LS(label, __rmc_noop()); }while(0)


#define __rmc_push_here_internal(l) do { L(l, __rmc_push()); VEDGE(pre, l); XEDGE(l, post); } while (0)
#define __rmc_push_here() __rmc_push_here_internal(XRCAT(__barrier_push, __COUNTER__))

#endif
