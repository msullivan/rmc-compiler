// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RCULIST_USER_RMC_SIMPLE
#define RCULIST_USER_RMC_SIMPLE

#include "epoch.hpp"
#include <rmc++.h>
#include <mutex>

namespace rmclib {

/// BEGIN SNIP
struct widget {
    unsigned key{0};
    unsigned val1{0}, val2{0};

    rmc::atomic<widget *> next{nullptr};
    widget *prev{nullptr};

    widget(unsigned pkey, unsigned pval1, unsigned pval2) :
        key(pkey), val1(pval1), val2(pval2) {}
    widget(widget *n, widget *p) : next(n), prev(p) {}
/// END SNIP
#if RCULIST_POISON
    // Poison the memory for testing purposes so that the world
    // is more likely to end of something goes wrong.
    ~widget() {
        widget *dead = reinterpret_cast<widget *>(0xdeaddead);
        next = prev = dead;
        key = val1 = val2 = 0xdeaddead;
    }
#endif
/// BEGIN SNIP
};

struct widgetlist {
    // A dummy widget is used as the head of the circular linked list.
    widget head{&head, &head};
    std::mutex write_lock;
};

widget *widget_find_give(widgetlist *list, unsigned key) noexcept;
widget *widget_find(widgetlist *list, unsigned key) noexcept;
void widget_insert(widgetlist *list, widget *obj) noexcept;
/// END SNIP


}

#endif
