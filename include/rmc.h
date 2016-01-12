// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_C_H
#define RMC_C_H

// Define the core RMC stuff
#include "rmc-core.h"

#if RMC_USE_SYSTEM_STDATOMIC
#include <stdatomic.h>
#else
#include <rmc-stdatomic.h>
#endif

#define __rmc_typeof(e) __typeof__(e)

#define rmc_push() __rmc_push()
#define rmc_push_here() __rmc_push_here()
#define rmc_bind_inside() __rmc_bind_inside()

// Now define the RMC atomic op instructions. We do this by using the
// C11 atomics at memory_order_relaxed.

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
