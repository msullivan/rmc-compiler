#include <rmc++.h>

extern void nus();
extern void run_in_dtor();
struct dtorable {
    ~dtorable() { run_in_dtor(); }
};

struct widget {
    int foo;
    int bar;
};

widget *get_widget(char *key);

// Some client code
int use_widget(char *key) {
    XEDGE_HERE(load_widget, a);

    dtorable lol;

    widget *w = LTAKE(load_widget, get_widget(key));
    nus();
    return L(a, w->foo) + L(a, w->bar);
}
