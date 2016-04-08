#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <atomic>
#include <stdio.h>

#include "util.hpp"
#include "parking.hpp"

using namespace rmclib;

// This is total garbage and needs to be a real test.

std::atomic<bool> go;
std::atomic<bool> ready2;
std::atomic<bool> signaled;
std::atomic<Parking::ThreadID> tid;

void work(int work = 50) {
    volatile int nus = 0;
    for (int i = 0; i < work; i++) {
        nus++;
    }
}


void t1() {
    tid = Parking::getCurrent();

    while (!go) {}

    work(rand() % 5000);

    while (!signaled) {
        Parking::park();
        printf("wakeup\n");
    }
    printf("out\n");
}

void t2() {
    ready2 = true;
    while (!go) {}

    work(rand() % 5000);

    signaled = true;
    Parking::unpark(tid);
    printf("signaled\n");
}

void dumbTest() {
    go = ready2 = signaled = tid = 0;

    std::vector<std::thread> threads;

    threads.push_back(std::thread(t1));
    threads.push_back(std::thread(t2));

    while (!tid || !ready2) {}
    go = true;
    joinAll(threads);
}

int main(int argc, char** argv) {
    for (int i = 0; i < 50; i++) {
        dumbTest();
    }
    printf("done with main test\n");

    // OK, now we timeout for 3s
    Parking::park_for(std::chrono::seconds(3));
    printf("slept some\n");
    Parking::park_until(
        std::chrono::system_clock::now() + std::chrono::seconds(3));
}
