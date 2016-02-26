#include <rmc.h>

#define NUM_WIDGETS 16

extern int calculate_idx(char *key);

typedef struct widget {
    int foo;
    int bar;
} widget;
typedef _Rmc(widget *) rmc_widget_ptr;

rmc_widget_ptr widgets[NUM_WIDGETS];

widget *butt;

__attribute__((noinline))
widget *get_widget(char *key) {
    rmc_bind_inside();
    XEDGE(get, ret);
    int idx = calculate_idx(key);
    widget *w = L(get, rmc_load(&widgets[idx]));
    return LGIVE(ret, w);
}

// Some client code
int use_widget(char *key) {
    rmc_bind_inside();
    XEDGE(load_widget, a);

    widget *w = LTAKE(load_widget, get_widget(key));
    return L(a, w->foo);
}
