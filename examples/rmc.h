#ifndef RMC_CORE_H
#define RMC_CORE_H

#ifdef HAS_RMC

/* We signal our labels and edges to our LLVM pass in a *really* hacky
 * way to avoid needing to modify the frontend.  Labelled statements
 * are wrapped in two goto labels to force them into their own basic
 * block (labelled statements don't fundamentally *need* to be in
 * their own basic block, but it makes things convenient) and edges
 * are specified by calling a dummy function __rmc_edge_register with
 * the names of the labels as arguments.
 *
 * This is really quite fragile; optimization passes will easily
 * destroy this information. The RMC pass should be run *before* any
 * real optimization passes are run but *after* mem2reg. */


/* C. */
#define RCAT(x,y)      x ## y
#define XRCAT(x,y)     RCAT(x,y)

#ifdef __cplusplus
extern "C" {
#endif

extern int __rmc_action_register(const char *name,
                                 void *entry, void *main, void *action);
extern int __rmc_edge_register(int is_vis, const char *src, const char *dst);
extern int __rmc_push(void);

#ifdef __cplusplus
}
#endif

#define RMC_EDGE(t, x, y) __rmc_edge_register(t, #x, #y)
#define XEDGE(x, y) RMC_EDGE(0, x, y)
#define VEDGE(x, y) RMC_EDGE(1, x, y)
/* This is unhygenic in a nasty way. */
/* The semis after the labels are because declarations can't directly
 * follow labels, apparently. */
#define LS_(name, label, stmt)                                     \
    __rmc_action_register(#name, &&XRCAT(__rmc_entry_, label), &&XRCAT(_rmc_, label), &&XRCAT(__rmc_end_, label)); \
    XRCAT(__rmc_entry_, label):                                  \
    XRCAT(_rmc_, label): ;                                       \
    stmt;                                                        \
    XRCAT(__rmc_end_, label): ;

#define LS(label, stmt) LS_(label, XRCAT(label##_, __COUNTER__), stmt)

#define PUSH __rmc_push()

#else

#include "atomic.h"

/* Dummy version that should work. */
#define XEDGE(x, y) do { } while (0)
#define VEDGE(x, y) do { } while (0)
/* Just stick a visibility barrier after every label. This isn't good
 * or anything, but it probably works. */
/* This is unhygenic in a nasty way. */
#define LS(label, stmt) stmt; vis_barrier()

#define PUSH smp_mb()

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

#define BARRIER_INTERNAL(l) do { L(l, PUSH); VEDGE(pre, l); XEDGE(l, post); } while (0)
#define BARRIER() BARRIER_INTERNAL(XRCAT(__barrier_push, __COUNTER__))


#endif
