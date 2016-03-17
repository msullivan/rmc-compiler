#ifndef RMC_GEN_PTR
#define RMC_GEN_PTR

#include <cstdint>
#include <cassert>
#include <type_traits>

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

#endif
