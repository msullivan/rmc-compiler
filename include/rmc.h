// Copyright (c) 2014-2017 Michael J. Sullivan
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



#define rmc_init(object, desired)                               \
    atomic_init(                                                \
        __rmc_atomic_fixup(object), desired)

#define rmc_compare_exchange_strong(object, expected, desired)          \
    atomic_compare_exchange_strong_explicit(                            \
        __rmc_atomic_fixup(object), expected,                           \
        desired, __rmc_rmw_order, __rmc_load_order)
#define rmc_compare_exchange(object, expected, desired)          \
    rmc_compare_exchange_strong(object, expected, desired)
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

// rmc_store needs to return a value to be used by L(). We don't
// bother returning a useful one.
#define rmc_store(object, desired)                                   \
    ({atomic_store_explicit(                                         \
        __rmc_atomic_fixup(object), desired, __rmc_store_order);     \
      0;})

#define rmc_load(object)                                             \
    atomic_load_explicit(                                            \
        __rmc_atomic_fixup(object), __rmc_load_order)


//////////////////////
// Sigh. SC versions of all of these things.

#define rmc_compare_exchange_strong_sc(object, expected, desired)          \
    atomic_compare_exchange_strong(                            \
        __rmc_atomic_fixup(object), expected,                           \
        desired)
#define rmc_compare_exchange_sc(object, expected, desired)          \
    rmc_compare_exchange_strong(object, expected, desired)
#define rmc_compare_exchange_weak_sc(object, expected, desired)            \
    atomic_compare_exchange_weak(                              \
        __rmc_atomic_fixup(object), expected,                           \
        desired)
#define rmc_exchange_sc(object, desired)                                   \
    atomic_exchange(                                           \
        __rmc_atomic_fixup(object), desired)
#define rmc_fetch_add_sc(object, operand)                               \
    atomic_fetch_add(                                       \
        __rmc_atomic_fixup(object), operand)
#define rmc_fetch_and_sc(object, operand)                               \
    atomic_fetch_and(                                       \
        __rmc_atomic_fixup(object), operand)
#define rmc_fetch_or_sc(object, operand)                                \
    atomic_fetch_or(                                        \
        __rmc_atomic_fixup(object), operand)
#define rmc_fetch_sub_sc(object, operand)                               \
    atomic_fetch_sub(                                       \
        __rmc_atomic_fixup(object), operand)
#define rmc_fetch_xor_sc(object, operand)                               \
    atomic_fetch_xor(                                       \
        __rmc_atomic_fixup(object), operand)

// rmc_store needs to return a value to be used by L(). We don't
// bother returning a useful one.
#define rmc_store_sc(object, desired)                                   \
    ({atomic_store(                                         \
        __rmc_atomic_fixup(object), desired);     \
      0;})

#define rmc_load_sc(object)                                             \
    atomic_load(                                            \
        __rmc_atomic_fixup(object))



#endif
