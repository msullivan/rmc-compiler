// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_CORE_H
#define RMC_CORE_H

#define RMC_FORCE_INLINE __attribute__((always_inline))
#define RMC_NODUPLICATE __attribute__((noduplicate))

#ifdef HAS_RMC

/* We signal our labels and edges to our LLVM pass in a fairly hacky
 * way to avoid needing to modify the frontend. Labelled statements
 * are bracketed by calls to the dummy functions __rmc_action_register
 * and __rmc_action_close which indicate the extent of the action and
 * associate it with a name. __rmc_action_close is passed the (bogus)
 * return value from __rmc_action_register in order to make them easy
 * to associate (even if they are duplicated by an optimizer (although
 * we ban this now)). Edges are specified by calling a dummy function
 * __rmc_edge_register with the names of the labels as arguments.
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
#define RMC_NOEXCEPT noexcept
extern "C" {
#else
#define RMC_NOEXCEPT
#endif

// RMC_NODUPLICATE prevents any transformation that would introduce
// more call sites to a function. This prevents some transformations
// that would cause problems by getting rid of the 1:1 correspondence
// of register() and close() calls as well as preventing inlining
// (except when there is exactly one call site).
// The extra dummy argument to __rmc_action_register is to prevent
// registers from getting merged when they have the same label.
// RMC_NOEXCEPT tells clang that they can't throw exceptions,
// so it will generate calls instead of invokes.
extern int __rmc_action_register(const char *name, int dummy)
  RMC_NOEXCEPT RMC_NODUPLICATE;
extern int __rmc_action_close(int x) RMC_NOEXCEPT RMC_NODUPLICATE;
extern int __rmc_edge_register(int edge_type, const char *src, const char *dst,
                               int bind_here)
  RMC_NOEXCEPT RMC_NODUPLICATE;
extern int __rmc_push(void) RMC_NOEXCEPT RMC_NODUPLICATE;

#ifdef __cplusplus
}
#endif

#define RMC_EDGE(t, x, y, h) __rmc_edge_register(t, #x, #y, h)

// This is unhygenic in a nasty way.
// Maybe we should throw some barrier()s in also, to be on the safe side?
#define LS_(name, label, stmt)                                   \
    int XRCAT(______lt, label) = __rmc_action_register(#name, __COUNTER__); \
    stmt;                                                        \
    __rmc_action_close(XRCAT(______lt, label));

#define LS(label, stmt) LS_(label, XRCAT(label##_, __COUNTER__), stmt)


#define LTRANSFER_(label, expr, is_take, ctr)         \
  L(label, ({                                         \
        extern __rmc_typeof(expr) XRCAT(__rmc_transfer_, ctr)(  \
          __rmc_typeof(expr), int) RMC_NOEXCEPT RMC_NODUPLICATE; \
        XRCAT(__rmc_transfer_, ctr)((expr), is_take);                  \
      }))
#define LTRANSFER(label, expr, is_take)         \
  LTRANSFER_(label, expr, is_take, __COUNTER__)


// What orders to use for the atomic ops. Always relaxed in the real version.
#define __rmc_load_order memory_order_relaxed
#define __rmc_store_order memory_order_relaxed
#define __rmc_rmw_order memory_order_relaxed

#else /* !HAS_RMC */

// The compiler doesn't support RMC, so we provide a backup
// implementation based on making all atomic operations sequentially
// consistent.

#define RMC_EDGE(t, x, y, h) do { } while (0)

#define LS(label, stmt) stmt
#define LTRANSFER(label, expr, is_take) L(label, expr)

// What orders to use for the atomic ops. We generally use seq_cst,
// but if RMC_DISABLE_PEDGE is set, then push edges are turned off,
// which means we can get away with using release/acquire for the
// memory orders.
#if !RMC_DISABLE_PEDGE || RMC_FALLBACK_USE_SC
#define __rmc_load_order memory_order_seq_cst
#define __rmc_store_order memory_order_seq_cst
#define __rmc_rmw_order memory_order_seq_cst
// Push is a no-op
#define __rmc_push() 0

#elif RMC_YOLO_FALLBACK /* !(using SC) */
// relaxed memory order, for convincing myself that any of this
// matters
#define __rmc_load_order memory_order_relaxed
#define __rmc_store_order memory_order_relaxed
#define __rmc_rmw_order memory_order_relaxed
#define __rmc_push() 0

#else /* !(using SC) && !(yolo fallback) */
// What orders to use for the atomic ops. Always release/acquire
#define __rmc_load_order memory_order_acquire
#define __rmc_store_order memory_order_release
#define __rmc_rmw_order memory_order_acq_rel

// Use __sync_synchronize instead of a C11 SC fence because I felt bad
// relying on details of the implementation of SC fences on our platforms
// (SC fences aren't actually as strong as pushes). Of course, the
// interactions between __sync_synchronize() (which is a "full sync")
// and C11 atomics are totally unspecified, so...
#define __rmc_push() ({ __sync_synchronize(); 0; })

#endif /* fallbacks */

#endif /* HAS_RMC */

// Shared stuff for both backends.

#define XEDGE(x, y) RMC_EDGE(0, x, y, 0)
#define VEDGE(x, y) RMC_EDGE(1, x, y, 0)
#define PEDGE(x, y) RMC_EDGE(2, x, y, 0)
// XXX: _HERE? Want a better name?
#define XEDGE_HERE(x, y) RMC_EDGE(0, x, y, 1)
#define VEDGE_HERE(x, y) RMC_EDGE(1, x, y, 1)
#define PEDGE_HERE(x, y) RMC_EDGE(2, x, y, 1)


#if RMC_DISABLE_PEDGE
#undef PEDGE
#undef PEDGE_HERE
#define PEDGE(a, b) __PEDGE_has_been_disabled_by_RMC_DISABLE_PEDGE
#define PEDGE_HERE(a, b) PEDGE(a, b)
#endif

// Nice way to extract a value directly from a named read
// without needing to manually stick it in a temporary.
// __rmc_typeof is defined in the C and C++ support files: in C++ we
// do template magic to turn rmc::atomic<T> into T, which will force
// the dereference.
#define L(label, expr)                                          \
  ({LS(label, __rmc_typeof(expr) _______t = expr); _______t;})

#define LGIVE(label, expr) LTRANSFER(label, expr, 0)
#define LTAKE(label, expr) LTRANSFER(label, expr, 1)

// Convenience macros for labels that have relationships with
// all program order successors or predecessors.
// The edge type is always visibility, since it implies execution order.
// Since no-ops are only meaningful for transitivity, the compiler is
// smart enough to do execution-order edge cutting if only execution
// edges are drawn.
#define __rmc_noop() ((void)0)
#define LPRE(label) do { VEDGE_HERE(pre, label); LS(label, __rmc_noop()); } while(0)
#define LPOST(label) do { VEDGE_HERE(label, post); LS(label, __rmc_noop()); }while(0)


#define __rmc_push_here_internal(l) do { L(l, __rmc_push()); VEDGE(pre, l); XEDGE(l, post); } while (0)
#define __rmc_push_here() __rmc_push_here_internal(XRCAT(__barrier_push, __COUNTER__))

#endif
