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

/// BEGIN SNIP
#define rculist_for_each_entry2(pos, h, tag_list, tag_use) \
    XEDGE_HERE(tag_list, tag_list); XEDGE_HERE(tag_list, tag_use); \
    for (pos = L(tag_list, (h)->next);                             \
         pos != (h);                                               \
         pos = L(tag_list, pos->next))
#define rculist_for_each_entry(pos, head, tag) \
    rculist_for_each_entry2(pos, head, __rcu_read, tag)

// Perform a lookup in an RCU-protected widgetlist, with
// execution edges drawn to the LGIVE return action.
// Must be done in an Epoch read-side critical section.
widget *widget_find_give(widgetlist *list, unsigned key) noexcept {
    widget *node;
    rculist_for_each_entry(node, &list->head, r) {
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


static void __rculist_insert_between(widget *n,
                                     widget *n1, widget *n2) {
    VEDGE(pre, link);
    n->prev = n1;
    n2->prev = n;
    n->next = n2;
    L(link, n1->next = n);
}
static void rculist_insert_before(widget *n, widget *n_old) {
    __rculist_insert_between(n, n_old->prev, n_old);
}
static void rculist_insert_tail(widget *n, widget *head) {
    rculist_insert_before(n, head);
}
static void rculist_replace(widget *n_old, widget *n_new) {
    __rculist_insert_between(n_new,
                             n_old->prev,
                             n_old->next);
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
        rculist_insert_tail(obj, &list->head);
        return;
    }

    rculist_replace(old, obj);

    // Wait until any readers that may be using the old node are gone
    // and then delete it.
    Epoch::rcuSynchronize();
    delete old;
}
/// END SNIP

}
