// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "rculist_rmc.hpp"
#include "rculist_user_rmc.hpp"
#include "epoch.hpp"
#include <mutex>


namespace rmclib {

/// BEGIN SNIP
// Perform a lookup in an RCU-protected widgetlist, with
// execution edges drawn to the LGIVE return action.
// Must be done in an Epoch read-side critical section.
widget *widget_find_give(widgetlist *list, unsigned key) noexcept {
    widget *node;
    rculist_for_each_entry(node, &list->head, link, r) {
        if (L(r, node->key) == key) {
            return LGIVE(r, node);
        }
    }

    return nullptr;
}
// Perform a lookup in an RCU-protected widgetlist with a traditional
// post edge.
// Must be done in an Epoch read-side critical section.
widget *widget_find(widgetlist *list, unsigned key) noexcept {
    XEDGE(find, post);
    return L(find, widget_find_give(list, key));
}

// Insert an object into a widgetlist, replacing an old object with
// the same key, if necessary.
// Must *not* be called from an Epoch read-side critical section.
void widget_insert(widgetlist *list, widget *obj) noexcept {
    // Acquires write_lock and automatically drops it when we leave scope.
    std::unique_lock<std::mutex> lock(list->write_lock);
    // We needn't give any constraints on the node lookup here.  Since
    // insertions always happen under the lock, any list modifications
    // are already visible to us.
/// END SNIP
    // There is some compilation fail here.

    // LLVM inlines widget_find_give and ought to be able to eliminate
    // the use of nullptr and a null check to signal a nonexistent
    // node and instead have that emerge implicitly from the control
    // flow. Unfortunately, the value obfuscation that rmc-compiler
    // does to make sure that LLVM doesn't break our data dependencies
    // also hides the fact that node is non-NULL when returning it.
/// BEGIN SNIP
    widget *old = widget_find_give(list, obj->key);

    // If nothing to replace we just insert it normally
    if (!old) {
        rculist_insert_tail(&obj->link, &list->head);
        return;
    }

    rculist_replace(&old->link, &obj->link);

    // Wait until any readers that may be using the old node are gone
    // and then delete it.
    Epoch::rcuSynchronize();
    delete old;
}
/// END SNIP

}
