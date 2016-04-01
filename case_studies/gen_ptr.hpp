#ifndef RMC_GEN_PTR
#define RMC_GEN_PTR

#include <cstdint>
#include <cassert>
#include <atomic>
#include <rmc++.h>

namespace rmclib {


template<typename T>
class alignas(2*sizeof(uintptr_t)) gen_ptr {
private:
    T ptr_{nullptr};
    uintptr_t gen_{0};

public:
    gen_ptr() noexcept {}
    gen_ptr(T t, uintptr_t gen) noexcept : ptr_(t), gen_(gen) { }

    uintptr_t gen() const { return gen_; }
    T ptr() const { return ptr_; }
    operator T() const { return ptr(); }
    T operator->() const { return ptr(); }
    auto operator*() -> decltype(*ptr()) { return *ptr(); }

    bool operator==(const gen_ptr<T> &rhs) const {
        return this->ptr_ == rhs.ptr_ && this->gen_ == rhs.gen_;
    }
    bool operator!=(const gen_ptr<T> &rhs) const { return !(*this == rhs); }

    gen_ptr<T> update(T n) { return gen_ptr<T>(n, gen()+1); }
};

}

// This is bullshit, right?
namespace std {

// A wrapper around std::atomic that stores a pointer with a
// generation and provides compare_exchange_{weak,strong}_gen()
// methods that automatically increment the generation counter in
// the expected value.
template<typename T>
class gen_atomic : public atomic<rmclib::gen_ptr<T>> {
public:
    using real_type = rmclib::gen_ptr<T>;

    real_type operator=(real_type desired) noexcept {
        this->store(desired); return desired;
    }

    bool compare_exchange_weak_gen(
        real_type& expected, T desired,
        std::memory_order order = std::memory_order_seq_cst) noexcept {

        return this->compare_exchange_weak(
            expected, expected.update(desired), order);
    }
    bool compare_exchange_strong_gen(
        real_type& expected, T desired,
        std::memory_order order = std::memory_order_seq_cst) noexcept {

        return this->compare_exchange_strong(
            expected, expected.update(desired), order);
    }
    bool compare_exchange_weak_gen(
        real_type& expected, T desired,
        std::memory_order success, std::memory_order failure) noexcept {

        return this->compare_exchange_weak(
            expected, expected.update(desired), success, failure);
    }
    bool compare_exchange_strong_gen(
        real_type& expected, T desired,
        std::memory_order success, std::memory_order failure) noexcept {

        return this->compare_exchange_strong(
            expected, expected.update(desired), success, failure);
    }


};

}

namespace rmc {

// Same as above, but for rmc
template<typename T>
class gen_atomic : public atomic<rmclib::gen_ptr<T>> {
public:
    using real_type = rmclib::gen_ptr<T>;

    real_type operator=(real_type desired) noexcept {
        this->store(desired); return desired;
    }

    bool compare_exchange_weak_gen(real_type& expected, T desired) noexcept {
        return this->compare_exchange_weak(
            expected, expected.update(desired));
    }
    bool compare_exchange_strong_gen(real_type& expected, T desired) noexcept {
        return this->compare_exchange_strong(
            expected, expected.update(desired));
    }
    bool compare_exchange_gen(real_type& expected, T desired) noexcept {
        return this->compare_exchange(
            expected, expected.update(desired));
    }

};
// Wee.
template<class T> struct remove_rmc<gen_atomic<T>> {
    typedef typename gen_atomic<T>::real_type type;
};

}


#endif
