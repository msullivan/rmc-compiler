#include <rmc.h>

int lol1(int *p) {
    L(a, 0);
    int r1 = *p;
    int r2 = *p;
    return r1 == r2;
}
int lol2(int *p) {
    L(a, 0);
    while (*p);
    return 1;
}


int lol3(int **p) {
    L(a, 0);
    return p[0][0] + p[0][1];
}
// Even with javaizing, this compiles to the code that lol3 *should* compile to.
// "Unordered" breaks CSE but not hoisting!
int lol4(int **p) {
    L(a, 0);
    int sum = 0;
    for (int i = 0; i < 2; i++) {
        sum += p[0][i];
    }
    return sum;
}

void lol_write(int *p) {
    L(a, 0);
    L(a, *p = 1);
    L(a, *p = 2);
}


#define N 100
int lol_loop(int **p) {
    L(a, 0);
    int sum = 0;
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            sum += p[i][2*j] + p[i][2*j+1];
        }
    }
    return sum;
}
