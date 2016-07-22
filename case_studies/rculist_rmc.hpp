// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RCULIST_RMC
#define RCULIST_RMC

// This is written in a very C style because I didn't want to bother
// with intrusive list stuff in C++.

#include <rmc++.h>
#include <stddef.h>
///////////////
namespace rmclib {


#define rcu_container_of(ptr, type, member) ({                        \
            const __typeof__( ((type *)0)->member ) *__mptr = (ptr);  \
            (type *)( (char *)__mptr - offsetof(type,member) );})


struct rculist_node {
    rmc::atomic<rculist_node *> next;
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
    VEDGE(pre, link);
    n->prev = n1;
    n2->prev = n;
    n->next = n2;
    L(link, n1->next = n);
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
                             n_old->next);
}


#define rculist_entry(ptr, type, member, tag)                           \
    rcu_container_of(L(tag, ptr.load()), type, member)

#define rculist_for_each_entry2(pos, h, member, tag_a, tag_b)                 \
    XEDGE_HERE(tag_a, tag_a); XEDGE_HERE(tag_a, tag_b); \
    for (pos = rculist_entry((h)->head.next, __typeof__(*pos), \
                             member, tag_a);                   \
         &pos->member != &(h)->head; \
         pos = rculist_entry(pos->member.next, __typeof__(*pos), member,tag_a))
#define rculist_for_each_entry(pos, head, member, tag) \
    rculist_for_each_entry2(pos, head, member, __rcu_read, tag)


}

#endif
