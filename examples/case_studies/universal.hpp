#ifndef RMC_UNIVERSAL
#define RMC_UNIVERSAL

#include <cstdint>
#include <utility>

namespace rmclib {

// Utility class to allow packing arbitrary stuff into a word that can
// be placed inside an atomic in a mildly intelligent way.  (Currently
// we skip the intelligence, though, and we always just allocate an
// object for it.)

class universal {
private:
    uintptr_t val_{0};

public:
    universal() noexcept {}

    template <typename T>
    explicit
    universal(const T &t) {
        T *p = new T(t);
        val_ = reinterpret_cast<uintptr_t>(p);
    }
    template <typename T>
    explicit
    universal(T &&t) {
        T *p = new T(std::move(t));
        val_ = reinterpret_cast<uintptr_t>(p);
    }

    template <typename T>
    T extract() {
        T *p = reinterpret_cast<T *>(val_);
        T ret(std::move(*p));
        delete p;
        return ret;
    }
};

}

#endif
