// List-traversal macros for the simple rculist setup.

// This is in its own file that gets included for bad reasons
// involving my janky infrastructure for including code snippets in my
// thesis document.

/// BEGIN SNIP
#define rculist_for_each_entry2(pos, h, tag_load, tag_use) \
    XEDGE_HERE(tag_load, tag_load); XEDGE_HERE(tag_load, tag_use); \
    for (pos = L(tag_load, (h)->next);                             \
         pos != (h);                                               \
         pos = L(tag_load, pos->next))
#define rculist_for_each_entry(pos, head, tag_use) \
    rculist_for_each_entry2(pos, head, __rcu_load, tag_use)

widget *widget_find_give2(widgetlist *list, unsigned key) noexcept {
    widget *node;
    rculist_for_each_entry(node, &list->head, r) {
        if (L(r, node->key) == key) {
            return LGIVE(r, node);
        }
    }

    return nullptr;
}
/// END SNIP
