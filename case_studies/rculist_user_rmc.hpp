// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RCULIST_USER_RMC
#define RCULIST_USER_RMC

#include "rculist_rmc.hpp"
#include "epoch.hpp"
#include <mutex>

namespace rmclib {

struct noob {
    unsigned key;
    unsigned val1, val2;
    rculist_node link;
    noob(unsigned pkey, unsigned pval1, unsigned pval2) :
        key(pkey), val1(pval1), val2(pval2) {}
#if RCULIST_POISON
    // Poison the memory for testing purposes so that the world
    // is more likely to end of something goes wrong.
    ~noob() {
        rculist_node *dead = reinterpret_cast<rculist_node *>(0xdeaddead);
        link.next = link.prev = dead;
        key = val1 = val2 = 0xdeaddead;
    }
#endif
};

struct nooblist {
    rculist_head head;
    std::mutex write_lock;
};

noob *noob_find_give(nooblist *list, unsigned key) noexcept;
noob *noob_find(nooblist *list, unsigned key) noexcept;
void noob_insert(nooblist *list, noob *obj) noexcept;


}

#endif
