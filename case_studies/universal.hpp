#ifndef RMC_UNIVERSAL
#define RMC_UNIVERSAL

#include <cstdint>
#include <cstring>
#include <utility>
#include <memory>

namespace rmclib {

// Utility class to allow packing arbitrary stuff into a word that can
// be placed inside an atomic in a mildly intelligent way:
//  * Trivially copyable objects that fit in a word are just copied
//  * std::unique_ptr<T> is released, copied, and recreated
//  * Everything else is moved into a heap allocation

namespace detail {

template<typename T>
struct trivially_to_uint {
    static const bool value =
        std::is_trivially_copyable<T>::value &&
        sizeof(T) <= sizeof(uintptr_t);
};

template<typename T, bool val = trivially_to_uint<T>::value>
struct to_uint;

template<typename T>
struct to_uint<std::unique_ptr<T>, false> {
    static uintptr_t into_uint(std::unique_ptr<T> &&t) {
        return reinterpret_cast<uintptr_t>(t.release());
    }
    static std::unique_ptr<T> from_uint(uintptr_t val) {
        return std::unique_ptr<T>(reinterpret_cast<T *>(val));
    }
};

template<typename T>
struct to_uint<T, false> {
    static uintptr_t into_uint(const T &t) {
        T *p = new T(t);
        return reinterpret_cast<uintptr_t>(p);
    }
    static uintptr_t into_uint(T &&t) {
        T *p = new T(std::move(t));
        return reinterpret_cast<uintptr_t>(p);
    }
    static T from_uint(uintptr_t val) {
        T *p = reinterpret_cast<T *>(val);
        T ret(std::move(*p));
        delete p;
        return ret;
    }
};

template<typename T>
struct to_uint<T, true> {
    // Are there better ways to do this??
    static uintptr_t into_uint(T t) {
        uintptr_t val;
        std::memcpy(&val, &t, sizeof(T));
        return val;
    }
    static T from_uint(uintptr_t val) {
        T t;
        std::memcpy(&t, &val, sizeof(T));
        return t;
    }
};


}

class universal {
private:
    uintptr_t val_{0};
    template<typename T>
    using to_uint = detail::to_uint<T>;

public:
    universal() noexcept {}

    template <typename T>
    explicit universal(const T &t) : val_(to_uint<T>::into_uint(t)) { }

    template <typename T>
    explicit universal(T &&t) :
        val_(to_uint<T>::into_uint(std::move(t))) { }

    template <typename T>
    T extract() {
        return to_uint<T>::from_uint(val_);
    }
};

}

#endif
