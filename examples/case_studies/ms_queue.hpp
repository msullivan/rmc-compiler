#if defined(USE_RMC_MS_QUEUE)
#include "ms_queue_rmc.hpp"
#elif defined(USE_LOCK_MS_QUEUE)
#include "ms_queue_lock.hpp"
#elif defined(USE_2LOCK_MS_QUEUE)
#include "ms_queue_2lock.hpp"
#elif defined(USE_SC_MS_QUEUE)
#include "ms_queue_sc.hpp"
#else
#error no implementation selected
#endif
