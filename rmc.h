#ifndef RMC_CORE_H
#define RMC_CORE_H

#include "atomic.h"

#ifdef HAS_RMC
#error "no you don't"
#else

/* Dummy version that should work. */
#define XEDGE(x, y) do { } while (0)
#define VEDGE(x, y) do { } while (0)
/* Just stick a visibility barrier after every label. This isn't good
 * or anything, but it probably works. */
/* This is unhygenic in a nasty way. */
#define L(label, stmt) stmt; vis_barrier()

#endif


#endif
