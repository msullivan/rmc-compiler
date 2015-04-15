// Hack to provide an assert macro that doesn't trigger unused
// variable warnings.

#ifndef SASSERT_H
#define SASSERT_H

#include <assert.h>

#ifdef NDEBUG
#define assert_(x) do { (void)(x); } while (0)
#else
#define assert_(x) assert(x)
#endif

#endif
