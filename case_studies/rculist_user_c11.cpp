// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "rculist_c11.hpp"
#include "rculist_user_c11.hpp"
#include "epoch.hpp"
#include <mutex>


namespace rmclib {
////////////
widget *widget_find(widgetlist *list, unsigned key) noexcept {
    widget *node;
    rculist_for_each_entry(node, &list->head, link) {
        if (node->key == key) {
            return node;
        }
    }

    return nullptr;
}

void widget_insert(widgetlist *list, widget *obj) noexcept {
    std::unique_lock<std::mutex> lock(list->write_lock);
    widget *old = widget_find(list, obj->key);

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
