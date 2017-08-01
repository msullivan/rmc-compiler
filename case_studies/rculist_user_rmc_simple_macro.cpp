// List-traversal macros for the simple rculist setup.

// This is in its own file that gets included for bad reasons
// involving my janky infrastructure for including code snippets in my
// thesis document.

/// BEGIN SNIP
#define rculist_for_each2(node, h, tag_load, tag_use) \
    XEDGE_HERE(tag_load, tag_load); XEDGE_HERE(tag_load, tag_use); \
    for (node = L(tag_load, (h)->next);                             \
         node != (h);                                               \
         node = L(tag_load, node->next))
#define rculist_for_each(node, head, tag_use) \
    rculist_for_each2(node, head, __rcu_load, tag_use)

widget *widget_find_fine2(widgetlist *list, unsigned key) noexcept {
    widget *node;
    rculist_for_each(node, &list->head, r) {
        if (L(r, node->key) == key) {
            return LGIVE(r, node);
        }
    }

    return nullptr;
}
/// END SNIP
