// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RCULIST_USER_RMC
#define RCULIST_USER_RMC

#include "rculist_rmc.hpp"
#include "epoch.hpp"
#include <mutex>

namespace rmclib {

/// BEGIN SNIP
struct widget {
    unsigned key;
    unsigned val1, val2;
    rculist_node link;
    widget(unsigned pkey, unsigned pval1, unsigned pval2) :
        key(pkey), val1(pval1), val2(pval2) {}
/// END SNIP
#if RCULIST_POISON
    // Poison the memory for testing purposes so that the world
    // is more likely to end of something goes wrong.
    ~widget() {
        rculist_node *dead = reinterpret_cast<rculist_node *>(0xdeaddead);
        link.next = link.prev = dead;
        key = val1 = val2 = 0xdeaddead;
    }
#endif
/// BEGIN SNIP
};

struct widgetlist {
    rculist_head head;
    std::mutex write_lock;
};

widget *widget_find_fine(widgetlist *list, unsigned key) noexcept;
widget *widget_find(widgetlist *list, unsigned key) noexcept;
void widget_insert(widgetlist *list, widget *obj) noexcept;
/// END SNIP


}

#endif
