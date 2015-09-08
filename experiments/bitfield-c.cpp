#include <atomic>
#include <thread>
#include <iostream>


struct thing {
    unsigned int a : 15;
    unsigned int b : 15;
};

thing x;

void f1(int n) {
    for (int i = 0; i < n; i++) {
        x.a++;
    }
}
void f2(int n) {
    for (int i = 0; i < n; i++) {
        x.b++;
    }
}

int main() {
    std::cout << sizeof(thing) << "\n";
    std::thread t1(f1, 1000000);
    std::thread t2(f2, 1000000);
    t1.join();
    t2.join();

    std::cout << x.a << " " << x.b << "\n";

    return 0;
}
