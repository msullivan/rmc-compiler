// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "rculist_rmc.hpp"
#include "rculist_user_rmc.hpp"
#include "epoch.hpp"
#include <mutex>


namespace rmclib {

noob *noob_find_give(nooblist *list, unsigned key) {
    noob *node;
    rculist_for_each_entry(node, &list->head, link, r) {
        if (L(r, node->key) == key) {
            return LGIVE(r, node);
        }
    }

    return nullptr;
}
noob *noob_find(nooblist *list, unsigned key) {
    XEDGE(find, post);
    return L(find, noob_find_give(list, key));
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
