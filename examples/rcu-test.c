#include <stddef.h>
#include "atomic.h"
#include "rmc.h"

///////////////

#define container_of(ptr, type, member) ({                              \
            const typeof( ((type *)0)->member ) *__mptr = (ptr);        \
            (type *)( (char *)__mptr - offsetof(type,member) );})


typedef struct list_node_t {
    struct list_node_t *next;
    struct list_node_t *prev;
} list_node_t;
typedef struct list_head_t {
    list_node_t head;
} list_head_t;

//

#define rcu_dereference(p)		({ \
            typeof(p) _value = ACCESS_ONCE(p);                          \
            smp_read_barrier_depends();                                 \
            (_value);                                                   \
        })

#define list_entry_rcu_linux(ptr, type, member) \
        container_of(rcu_dereference(ptr), type, member)

#define list_for_each_entry_rcu_linux(pos, head, member) \
        for (pos = list_entry_rcu_linux((head)->head.next, typeof(*pos), member); \
             &pos->member != &(head)->head;                              \
             pos = list_entry_rcu_linux(pos->member.next, typeof(*pos), member))


#define list_entry_rcu_rmc(ptr, type, member, tag)  \
        container_of(L(tag, ptr), type, member)


#define list_for_each_entry_rcu_rmc2(pos, head, member, tag_a, tag_b) \
        XEDGE(tag_a, tag_a); XEDGE(tag_a, tag_b);                       \
        for (pos = list_entry_rcu_rmc((head)->head.next, typeof(*pos), member, tag_a); \
             &pos->member != &(head)->head;                              \
             pos = list_entry_rcu_rmc(pos->member.next, typeof(*pos), member, tag_a))

#define list_for_each_entry_rcu_rmc(pos, head, member, tag) \
            list_for_each_entry_rcu_rmc2(pos, head, member, __rcu_read, tag)


/////////////////


#define rcu_read_lock() do { } while (0)
#define rcu_read_unlock() LS(__dummy_awful_hack, PUSH)


/////
typedef struct noob_node_t {
    struct noob_node_t *next;
    int key;
    int val;
} noob_node_t;

typedef struct test_node_t {
    int key;
    int val;
    list_node_t l;
} test_node_t;



////////////

int noob_search_rmc(noob_node_t **head, int key) {
    int res = -1;
    XEDGE(a, b);
    XEDGE(a, a);

    rcu_read_lock();
    for (noob_node_t *node = L(a, *head); node; node = L(a, node->next)) {
        if (L(b, node->key) == key) {
            res = L(b, node->val);
            break;
        }
    }

    rcu_read_unlock();
    return res;
}


int list_search_linux(list_head_t *head, int key) {
    int res = -1;
    test_node_t *node;
    //rcu_read_lock();
    list_for_each_entry_rcu_linux(node, head, l) {
        if (node->key == key) {
            res = node->val;
            break;
        }
    }

    //rcu_read_unlock();
    return res;
}

int list_search_rmc(list_head_t *head, int key) {
    int res = -1;
    test_node_t *node;
    rcu_read_lock();
    list_for_each_entry_rcu_rmc(node, head, l, r) {
        if (L(r, node->key) == key) {
            res = L(r, node->val);
            break;
        }
    }

    rcu_read_unlock();
    return res;
}
