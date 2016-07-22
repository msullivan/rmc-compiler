// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef LAMBDA_WRAP
#define LAMBDA_WRAP

namespace rmclib {

// Bullshit workaround to get lambdas stored internally with older
// versions of libstdc++ that predate compiler support for
// is_trivially_copyable.
// No safeties or whatever.
// F ought to be trivially copyable or you deserve what you get.
template <typename F, typename R>
struct bs_functor_wrapper {};

template<typename F, typename R, typename... Args>
struct bs_functor_wrapper<F, R(Args...)> {
	F functor_;
	bs_functor_wrapper(F &&functor) : functor_(std::move(functor)) {}
	R operator()(Args... args) {
		return functor_(std::forward<Args>(args)...);
	}
};
template<typename R, typename F>
bs_functor_wrapper<F, R> wrap_lambda(F &&f) {
	return bs_functor_wrapper<F, R>(std::move(f));
}

}

namespace std {
// libstdc++ uses the __is_location_invariant struct to determine
// whether something can be stored internally in a
// std::function. Manually indicate that we meet that.
template<typename F, typename R>
struct __is_location_invariant<rmclib::bs_functor_wrapper<F, R>> :
		integral_constant<bool, true> {};
}

#endif
