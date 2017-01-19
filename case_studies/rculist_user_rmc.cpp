// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "rculist_rmc.hpp"
#include "rculist_user_rmc.hpp"
#include "epoch.hpp"
#include <mutex>


namespace rmclib {

noob *noob_find_give(nooblist *list, unsigned key) noexcept {
    noob *node;
    rculist_for_each_entry(node, &list->head, link, r) {
        if (L(r, node->key) == key) {
            return LGIVE(r, node);
        }
    }

    return nullptr;
}
noob *noob_find(nooblist *list, unsigned key) noexcept {
    XEDGE(find, post);
    return L(find, noob_find_give(list, key));
}

void noob_insert(nooblist *list, noob *obj) noexcept {
    std::unique_lock<std::mutex> lock(list->write_lock);
    // We needn't give any constraints on the node lookup here.  Since
    // insertions always happen under the lock, any list modifications
    // are already visible to us.

    // There is some compilation fail here.

    // LLVM inlines noob_find_give and ought to be able to eliminate
    // the use of nullptr and a null check to signal a nonexistent
    // node and instead have that emerge implicitly from the control
    // flow. Unfortunately, the value obfuscation that rmc-compiler
    // does to make sure that LLVM doesn't break our data dependencies
    // also hides the fact that node is non-NULL when returning it.
    noob *old = noob_find_give(list, obj->key);

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
