#include "parking.hpp"

namespace rmclib {

#if USE_FUTEX_PARKING
thread_local Parking::ThreadNode Parking::me_;
#elif USE_PTHREAD_PARKING
thread_local Parking::ThreadNode Parking::me_ = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    false
};
#else
thread_local Parking::ThreadNode Parking::me_;
#endif

}
