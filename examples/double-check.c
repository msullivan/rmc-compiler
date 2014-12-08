#include "rmc.h"

struct mutex_t;
struct foo_t;

typedef struct mutex_t mutex_t;
typedef struct foo_t foo_t;

extern void mutex_lock(mutex_t *p);
extern void mutex_unlock(mutex_t *p);
extern foo_t *new_foo(void);

extern mutex_t *foo_lock;


foo_t *get_foo(void) {
    static foo_t *single_foo = 0;

    XEDGE(read, post);
    VEDGE(construct, update);

    foo_t *r = L(read, single_foo);
    if (r != 0) return r;

    mutex_lock(foo_lock);
    L(read, r = single_foo);
    if (r == 0) {
        L(construct, r = new_foo());
        L(update, single_foo = r);
    }
    mutex_unlock(foo_lock);
    return r;
}
