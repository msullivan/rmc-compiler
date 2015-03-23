// Copyright (c) 2014-2015 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_CPP_H
#define RMC_CPP_H

// Define the core RMC stuff
#include "rmc-core.h"
#include <atomic>

// XXX: namespacing?

template<typename T>
class rmc {
private:
  std::atomic<T> val;

public:
  rmc() = default;
  constexpr rmc(T desired) : val(desired) {}
  rmc(const rmc&) = delete;

  rmc& operator=(const rmc&) = delete;

  void store(T desired) { val.store(desired, std::__rmc_load_order); }
  T operator=(T desired) { store(desired); return desired; }

  T load() const { return val.load(std::__rmc_store_order); }
  operator T() const { return load(); }

};

#include <type_traits>
template<class T> struct remove_rmc           {typedef T type;};
template<class T> struct remove_rmc<rmc<T>>   {typedef T type;};
// Strip references and rmc<> things out of the type inferred for an
// L() action. This forces coercions and loads to happen inside the
// action.
#define __rmc_typeof(e)                                         \
  remove_rmc<std::remove_reference<decltype(e)>::type>::type



#endif
