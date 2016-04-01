#include "parking.hpp"

#define USE_PTHREAD_PARKING 1

#if USE_PTHREAD_PARKING
thread_local Parking::ThreadNode Parking::me_ = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    false
};
#else
thread_local Parking::ThreadNode Parking::me_;
#endif
