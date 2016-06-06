#ifndef RCULIST_C11
#define RCULIST_C11

// This is written in a very C style because I didn't want to bother
// with intrusive list stuff in C++.

#include <stddef.h>
#include <atomic>

///////////////
namespace rmclib {


#define rcu_container_of(ptr, type, member) ({                        \
            const __typeof__( ((type *)0)->member ) *__mptr = (ptr);  \
            (type *)( (char *)__mptr - offsetof(type,member) );})


struct rculist_node {
    std::atomic<rculist_node *> next;
    rculist_node * prev;

    rculist_node() : next(nullptr), prev(nullptr) {}
    explicit
    rculist_node(int is_head) : next(this), prev(this) {}
};
struct rculist_head {
    rculist_node head{1338};
};


// Some basic routines
static void __rculist_insert_between(rculist_node *n,
                                     rculist_node *n1, rculist_node *n2) {
    n->prev = n1;
    n2->prev = n;
    n->next.store(n2, std::memory_order_relaxed);
    n1->next.store(n, std::memory_order_release);
}
static void rculist_insert_before(rculist_node *n, rculist_node *n_old) {
    __rculist_insert_between(n, n_old->prev, n_old);
}
static void rculist_insert_tail(rculist_node *n, rculist_head *head) {
    rculist_insert_before(n, &head->head);
}
static void rculist_replace(rculist_node *n_old, rculist_node *n_new) {
    __rculist_insert_between(n_new,
                             n_old->prev,
                             n_old->next.load(std::memory_order_relaxed));
}


#define rculist_entry(ptr, type, member) \
    rcu_container_of(ptr.load(std::memory_order_consume), type, member)

#define rculist_for_each_entry(pos, h, member) \
    for (pos = rculist_entry((h)->head.next, __typeof__(*pos), member); \
         &pos->member != &(h)->head; \
         pos = rculist_entry(pos->member.next, __typeof__(*pos), member))


}

#endif
