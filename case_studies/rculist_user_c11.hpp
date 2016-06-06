#ifndef RCULIST_USER_C11
#define RCULIST_USER_C11

#include "rculist_c11.hpp"
#include "epoch.hpp"
#include <mutex>

namespace rmclib {

struct noob {
    unsigned key;
    unsigned val1, val2;
    rculist_node link;
    noob(unsigned pkey, unsigned pval1, unsigned pval2) :
        key(pkey), val1(pval1), val2(pval2) {}
    // Poison the memory for testing purposes so that the world
    // is more likely to end of something goes wrong.
    ~noob() {
        rculist_node *dead = reinterpret_cast<rculist_node *>(0xdeaddead);
        link.next.store(dead, std::memory_order_relaxed);
        link.prev = dead;
        key = val1 = val2 = 0xdeaddead;
    }
};

struct nooblist {
    rculist_head head;
    std::mutex write_lock;
};


////////////
noob *noob_find(nooblist *list, unsigned key) {
    noob *node;
    rculist_for_each_entry(node, &list->head, link) {
        if (node->key == key) {
            return node;
        }
    }

    return nullptr;
}

void noob_insert(nooblist *list, noob *obj) {
    std::unique_lock<std::mutex> lock(list->write_lock);
    noob *old = noob_find(list, obj->key);

    // If nothing to replace we just insert it normally
    if (!old) {
        rculist_insert_tail(&obj->link, &list->head);
        return;
    }

    rculist_replace(&old->link, &obj->link);

    // Whatever, we'll do it with synchronizing instead of unlink
    Epoch::rcuSynchronize();
    delete old;
}

}

#endif
