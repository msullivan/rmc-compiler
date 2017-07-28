// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RCULIST_C11
#define RCULIST_C11

// This is written in a very C style because I didn't want to bother
// with intrusive list stuff in C++.

#include <stddef.h>
#include <atomic>

///////////////
namespace rmclib {

// So that we can benchmark how fast C11 would be if consume actually
// worked.
#ifdef USE_FAKE_CONSUME
template <typename T>
T rculist_consume(std::atomic<T> &val) {
    // XXX: this isn't guarenteed to work of course
    // I think that *probably* the compiler won't mess with us, and I
    // don't care about ALPHA. Should I bother doing more?
    return val.load(std::memory_order_relaxed);
}
#elif defined(USE_FENCED_CONSUME)
// For testing whether fences are way worse than acquires.
template <typename T>
T rculist_consume(std::atomic<T> &val) {
    T x = val.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return x;
}
#else
template <typename T>
T rculist_consume(std::atomic<T> &val) {
    return val.load(std::memory_order_consume);
}
#endif


#define rcu_container_of(ptr, type, member) ({                        \
            const __typeof__( ((type *)0)->member ) *__mptr = (ptr);  \
            (type *)( (char *)__mptr - offsetof(type,member) );})


struct rculist_node {
    std::atomic<rculist_node *> next;
    rculist_node * prev;

    rculist_node() : next(nullptr), prev(nullptr) {}
    rculist_node(rculist_node *n, rculist_node *p) : next(n), prev(p) {}
};
struct rculist_head {
    rculist_node head{&head, &head};
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
static void rculist_remove(rculist_node *n_old) {
    rculist_node *next = n_old->next.load(std::memory_order_relaxed);
    rculist_node *prev = n_old->prev;
    next->prev = prev;
    prev->next.store(next, std::memory_order_release);
}


#define rculist_entry(ptr, type, member) \
    rcu_container_of(rculist_consume(ptr), type, member)

#define rculist_for_each_entry(pos, h, member) \
    for (pos = rculist_entry((h)->head.next, __typeof__(*pos), member); \
         &pos->member != &(h)->head; \
         pos = rculist_entry(pos->member.next, __typeof__(*pos), member))


}

#endif
