#include <rmc.h>

#define NUM_WIDGETS 16

extern int calculate_idx(char *key);

typedef struct widget {
    int foo;
    int bar;
} widget;
typedef _Rmc(widget *) rmc_widget_ptr;

rmc_widget_ptr widgets[NUM_WIDGETS];

__attribute__((noinline))
widget *get_widget(char *key) {
    XEDGE_HERE(get, ret);
    int idx = calculate_idx(key);
    widget *w = L(get, rmc_load(&widgets[idx]));
    return LGIVE(ret, w);
}

// Some client code
int use_widget(char *key) {
    XEDGE_HERE(load_widget, a);

    widget *w = LTAKE(load_widget, get_widget(key));
    return L(a, w->foo) + L(a, w->bar);
}

//
__attribute__((noinline))
int consume_widget(widget *w) {
    LTAKE(w, w);
    XEDGE_HERE(w, ret);
    return L(ret, w->foo);
}

int pass_widget() {
    XEDGE_HERE(get, pass);
    widget *w = L(get, rmc_load(&widgets[0]));
    return consume_widget(LGIVE(pass, w));
}

int givetake_widget(char *key) {
    XEDGE_HERE(get, pass);
    widget *w = LTAKE(get, get_widget(key));
    return consume_widget(LGIVE(pass, w));
}
