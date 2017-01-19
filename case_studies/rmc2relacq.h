// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

// Use an egregious hack to turn an RMC version (without PEDGE) of
// something into a pure release_acquire version.

#ifndef RMC_2_RELACQ
#define RMC_2_RELACQ

#ifdef RMC_CORE_H
#error Too late to pull off rmc2relacq nonsenes!
#endif

// Force using the fallback
#undef HAS_RMC
// And disable PEDGE so that it will use rel/acq instead of seq_cst
#define RMC_DISABLE_PEDGE 1
#include <rmc-core.h>

#endif
