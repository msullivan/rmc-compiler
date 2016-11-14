// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

// Use an egregious hack to turn an RMC version of
// something into a pure SC version.

#ifndef RMC_2_RELACQ
#define RMC_2_RELACQ

#ifdef RMC_CORE_H
#error Too late to pull off rmc2sc nonsenes!
#endif

// Force using the fallback
#undef HAS_RMC
#define RMC_FALLBACK_USE_SC 1
#include <rmc-core.h>

#endif
