#ifndef RMC_CORE_H
#define RMC_CORE_H

#ifdef HAS_RMC

/* We signal our labels and edges to our LLVM pass in a *really* hacky
 * way to avoid needing to modify the frontend.  Labelled statements
 * are wrapped in two goto labels to force them into their own basic
 * block (labelled statements don't fundamentally *need* to be in
 * their own basic block, but it makes things convenient) and edges
 * are specified by calling a dummy function __rmc_edge_register with
 * the labels as arguments (using computed goto to get the labels).
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

extern void __rmc_edge_register(int is_vis, char *src, char *dst);

#ifdef __cplusplus
}
#endif


#define RMC_EDGE(t, x, y) __rmc_edge_register(t, #x, #y)
#define XEDGE(x, y) RMC_EDGE(0, x, y)
#define VEDGE(x, y) RMC_EDGE(1, x, y)
/* This is unhygenic in a nasty way. */
/* The (void)0s are because declarations can't directly follow labels,
 * apparently. */
#define L(label, stmt)                                                    \
    XRCAT(_rmc_##label##_, __COUNTER__): __attribute__((unused)) (void)0; \
    stmt;                                                                 \
    XRCAT(__rmc_end_##label##_, __COUNTER__): __attribute__((unused)) (void)0

#else

#include "atomic.h"

/* Dummy version that should work. */
#define XEDGE(x, y) do { } while (0)
#define VEDGE(x, y) do { } while (0)
/* Just stick a visibility barrier after every label. This isn't good
 * or anything, but it probably works. */
/* This is unhygenic in a nasty way. */
#define L(label, stmt) stmt; vis_barrier()

#endif /* HAS_RMC */

#endif
