// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_CPP_H
#define RMC_CPP_H

// Define the core RMC stuff
#include "rmc-core.h"
#include <atomic>

#include <type_traits>

namespace rmc {

// RMC_YOLO_FALLBACK exists to test how badly things fail if we
// /don't/ insert any hardware barriers. For those tests are are only
// interested in what the hardware does, and not the compiler, so we
// need some way to suppress any trouble from the compiler. To that
// end, we have barrier_dummy, which sticks a compiler barrier in its
// ctor and dtor. Toss in a method and it keeps the contents from
// getting reordered with anything external.
// This is kind of frumious.
#if RMC_YOLO_FALLBACK
struct barrier_dummy {
  static inline void barrier() { __asm__ __volatile__("":::"memory"); }
  inline barrier_dummy() { barrier(); }
  inline ~barrier_dummy() { barrier(); }
};
#else
struct barrier_dummy {
  inline ~barrier_dummy() { } // dtor prevents unused var warnings
};
#endif


// A value that can be concurrently accessed by multiple threads
// safely, in the RMC atomics framework.
// Implemented as a wrapper around std::atomic that uses different
// memory orders.
template<typename T,
		 std::memory_order store_ord = std::__rmc_store_order,
		 std::memory_order load_ord = std::__rmc_load_order,
		 std::memory_order rmw_ord = std::__rmc_rmw_order
		 >
class atomic {
private:
  // If the underlying type is a pointer, then we use ptrdiff_t as the
  // argument to fetch_add and fetch_sub; otherwise we use the type
  // itself.
  using arith_arg_type = typename
    std::conditional<std::is_pointer<T>::value, std::ptrdiff_t, T>::type;

  std::atomic<T> val;

public:
  atomic() = default;
  ~atomic() = default;
  constexpr atomic(T desired) noexcept : val(desired) {}
  atomic(const atomic&) = delete;
  atomic& operator=(const atomic&) = delete;
  T operator=(T desired) noexcept { store(desired); return desired; }
  operator T() const noexcept { return load(); }

  void store(T desired) noexcept {
    barrier_dummy dummy;
    val.store(desired, store_ord);
  }
  T load() const noexcept {
    barrier_dummy dummy;
    return val.load(load_ord);
  }

  bool compare_exchange_weak(T& expected, T desired) noexcept {
    barrier_dummy dummy;
    return val.compare_exchange_weak(
      expected, desired, rmw_ord, load_ord);
  }
  bool compare_exchange_strong(T& expected, T desired) noexcept {
    barrier_dummy dummy;
    return val.compare_exchange_strong(
      expected, desired, rmw_ord, load_ord);
  }
  bool compare_exchange(T& expected, T desired) noexcept {
    barrier_dummy dummy;
    return this->compare_exchange_strong(expected, desired);
  }
  T exchange(T desired) noexcept {
    barrier_dummy dummy;
    return val.exchange(desired, rmw_ord);
  }

  // Arithmetic RMWs. Fails if the underlying type isn't integral or pointer
  T fetch_add(arith_arg_type arg) noexcept {
    barrier_dummy dummy;
    return val.fetch_add(arg, rmw_ord);
  }
  T fetch_sub(arith_arg_type arg) noexcept {
    barrier_dummy dummy;
    return val.fetch_sub(arg, rmw_ord);
  }
  T fetch_and(T arg) noexcept {
    barrier_dummy dummy;
    return val.fetch_and(arg, rmw_ord);
  }
  T fetch_or(T arg) noexcept {
    barrier_dummy dummy;
    return val.fetch_or(arg, rmw_ord);
  }
  T fetch_xor(T arg) noexcept {
    barrier_dummy dummy;
    return val.fetch_xor(arg, rmw_ord);
  }

  // We could add operator overloading, but I'm not sure if I approve
  // of it in this circumstance.

  // Get the underlying std::atomic. This is for perpetrating various
  // terrible nonsense.
  std::atomic<T> &get_std_atomic() noexcept { return val; }
};

template<class T>
using sc_atomic = atomic<T,
						 std::memory_order_seq_cst,
						 std::memory_order_seq_cst,
						 std::memory_order_seq_cst>;



template<class T> struct remove_rmc              {typedef T type;};
template<class T> struct remove_rmc<atomic<T>>   {typedef T type;};
// Strip references and rmc::atomic<> things out of the type inferred
// for an L() action. This forces coercions and loads to happen inside
// the action.
#define __rmc_typeof(e)                                         \
  typename ::rmc::remove_rmc<typename std::remove_reference<decltype(e)>::type>::type

RMC_FORCE_INLINE
static inline int push() { return __rmc_push(); }
RMC_FORCE_INLINE
static inline void push_here() { __rmc_push_here(); }

}

#endif
