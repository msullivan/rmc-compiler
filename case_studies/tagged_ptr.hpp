#ifndef RMC_TAGGED_PTR
#define RMC_TAGGED_PTR

#include <cstdint>
#include <cassert>
#include <type_traits>

namespace rmclib {


template< class >
struct ptr_data;

// A pointer (or pointer-like-object) in which we store a small
// integer in the low bits. Inspired by a class used in LLVM.

// The tag bits are stored in the highest of the available low order
// bits to enable tagged_ptr to be stacked. This probably isn't worth
// it.
template<typename T, int bits = 1>
class tagged_ptr {
private:
    uintptr_t val_;

    static const uintptr_t kTagMask = (1 << bits) - 1;
    static const uintptr_t kPtrMask = ~((1 << ptr_data<T>::kAvailBits) - 1);
    static const uintptr_t kShift = ptr_data<T>::kAvailBits - bits;
    static_assert(ptr_data<T>::kAvailBits >= bits, "Has enough bits left");

    friend ptr_data<tagged_ptr<T, bits>>;

public:
    tagged_ptr() noexcept : val_(0) {}
    tagged_ptr(T t) noexcept : tagged_ptr(t, 0) {}
    tagged_ptr(T t, uintptr_t tag) noexcept {
        uintptr_t ptrInt = ptr_data<T>::toInt(t);
        assert((ptrInt & ~kPtrMask) == 0);
        assert((tag & ~kTagMask) == 0);

        val_ = ptrInt | (tag << kShift);
    }

    operator T() const { return ptr(); }

    T ptr() const {
        return ptr_data<T>::toPtr(val_ & kPtrMask);
    }
    uintptr_t tag() const {
        return (val_ >> kShift) & kTagMask;
    }
    T operator->() const { return ptr(); }

    bool operator==(tagged_ptr<T, bits> rhs) { return val_ == rhs.val_; }
    bool operator!=(tagged_ptr<T, bits> rhs) { return !(*this == rhs); }


    auto operator*() -> decltype(*ptr()) { return *ptr(); }

};

// Templates for data that can go in pointer part of of a tagged_ptr.
// These include actual pointers and other tagged_ptrs.
// ptr_data<T> provides kAvailBits, which describes how many free bits there are

template<typename T>
struct ptr_data<T *> {
    static const uintptr_t kAvailBits = 3;
    /*
    static const uintptr_t kAlign = std::alignment_of<T>::value;
    static const uintptr_t kAvailBits =
        kAlign >= 8 ? 3 :
        kAlign == 4 ? 2 :
        kAlign == 2 ? 1 : 0;
    static_assert(kAvailBits > 0, "Has reasonable alignment");
    */
    static uintptr_t toInt(T *t) { return reinterpret_cast<uintptr_t>(t); }
    static T *toPtr(uintptr_t t) { return reinterpret_cast<T*>(t); }
};

template<typename T, int bits>
struct ptr_data<tagged_ptr<T, bits>> {
    static const uintptr_t kAvailBits = tagged_ptr<T, bits>::kShift;;
    static uintptr_t toInt(tagged_ptr<T, bits> t) { return t.val_; }
    static tagged_ptr<T, bits> toPtr(uintptr_t t) {
        tagged_ptr<T, bits> val;
        val.val_ = t;
        return val;
    }
};



}

#endif
