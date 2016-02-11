#if defined(USE_LEAK_EPOCH)
#include "epoch_leak.hpp"
#elif defined(USE_SC_EPOCH)
#include "epoch_sc.hpp"
#else
#error no implementation selected
#endif
