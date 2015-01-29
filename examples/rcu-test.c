#include "rmc.h"

typedef struct node_t {
    struct node_t *next;
    int key;
    int val;
} node_t;

#define rcu_read_lock() do { } while (0)
#define rcu_read_unlock() L(__dummy_awful_hack, PUSH)


int search(node_t **head, int key) {
    int res = -1;
    XEDGE(a, b);
    XEDGE(a, a);

    rcu_read_lock();
    for (node_t *node = L(a, *head); node; node = L(a, node->next)) {
        if (L(b, node->key) == key) {
            res = L(b, node->val);
            break;
        }
    }

    rcu_read_unlock();
    return res;
}
