#define _GNU_SOURCE

#include <unistd.h>
#include "atomic.h"
#include <rmc.h>
#include <stddef.h>

typedef struct noob_node_t {
    _Rmc(struct noob_node_t*) next;
    int key;
    int val;
} noob_node_t;


////////////

int noob_search_rmc(_Rmc(noob_node_t *) *head, int key) {
    int res = -1;
    XEDGE_HERE(a, b);
    XEDGE_HERE(a, a);

    for (noob_node_t *node = L(a, rmc_load(head)); node;
         node = L(a, rmc_load(&node->next))) {
        if (L(b, node->key) == key) {
            res = L(b, node->val);
            break;
        }
    }

    return res;
}
