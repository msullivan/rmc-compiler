// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_CPP_H
#define RMC_CPP_H

// Define the core RMC stuff
#include "rmc-core.h"
#include <atomic>

#include <type_traits>

namespace rmc {

// A value that can be concurrently accessed by multiple threads
// safely, in the RMC atomics framework.
// Implemented as a wrapper around std::atomic that uses different
// memory orders.
template<typename T>
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

  void store(T desired) noexcept {val.store(desired, std::__rmc_store_order); }
  T load() const noexcept { return val.load(std::__rmc_load_order); }

  bool compare_exchange_weak(T& expected, T desired) noexcept {
    return val.compare_exchange_weak(
      expected, desired, std::__rmc_rmw_order, std::__rmc_load_order);
  }
  bool compare_exchange_strong(T& expected, T desired) noexcept {
    return val.compare_exchange_strong(
      expected, desired, std::__rmc_rmw_order, std::__rmc_load_order);
  }
  bool compare_exchange(T& expected, T desired) noexcept {
    return this->compare_exchange_strong(expected, desired);
  }
  T exchange(T desired) noexcept {
    return val.exchange(desired, std::__rmc_rmw_order);
  }

  // Arithmetic RMWs. Will fail if the underlying type isn't integral or pointer
  T fetch_add(arith_arg_type arg) noexcept {
    return val.fetch_add(arg, std::__rmc_rmw_order);
  }
  T fetch_sub(arith_arg_type arg) noexcept {
    return val.fetch_sub(arg, std::__rmc_rmw_order);
  }
  T fetch_and(T arg) noexcept {
    return val.fetch_and(arg, std::__rmc_rmw_order);
  }
  T fetch_or(T arg) noexcept {
    return val.fetch_or(arg, std::__rmc_rmw_order);
  }
  T fetch_xor(T arg) noexcept {
    return val.fetch_xor(arg, std::__rmc_rmw_order);
  }

  // We could add operator overloading, but I'm not sure if I approve
  // of it in this circumstance.
};


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
