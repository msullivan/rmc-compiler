// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

// This is a version of of rculist_user_rmc that open codes all the
// list manipulation crap in instead of abstracting it away, in order
// to simplify it as an example.

#include "rculist_user_rmc_simple.hpp"
#include "epoch.hpp"
#include <mutex>

namespace rmclib {

#include "rculist_user_rmc_simple_macro.cpp"

/// BEGIN SNIP
// Perform a lookup in an RCU-protected widgetlist, with
// execution edges drawn to the LGIVE return action.
// Must be done in an Epoch read-side critical section.
widget *widget_find_give(widgetlist *list, unsigned key) noexcept {
    XEDGE_HERE(load, load);
    XEDGE_HERE(load, use);
    widget *node;
    widget *head = &list->head;
    for (node = L(load, head->next); node != head; node = L(load, node->next)) {
        if (L(use, node->key) == key) {
            return LGIVE(use, node);
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

static void insert_between(widget *n, widget *n1, widget *n2) {
    VEDGE(pre, link);
    n->prev = n1;
    n2->prev = n;
    n->next = n2;
    L(link, n1->next = n);
}
static void insert_before(widget *n, widget *n_old) {
    insert_between(n, n_old->prev, n_old);
}
static void replace(widget *n_old, widget *n_new) {
    insert_between(n_new, n_old->prev, n_old->next);
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
    widget *old = widget_find_give(list, obj->key);

    // If nothing to replace we just insert it normally
    if (!old) {
        insert_before(obj, &list->head);
        return;
    }

    replace(old, obj);

    // Wait until any readers that may be using the old node are gone
    // and then delete it.
    Epoch::rcuSynchronize();
    delete old;
}
/// END SNIP

}
