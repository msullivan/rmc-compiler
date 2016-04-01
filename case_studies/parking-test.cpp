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

void t1() {
  printf("hello t1\n");
  tid = Parking::getCurrent();

  while (!go) {}

  while (!signaled) {
    Parking::park();
    printf("wakeup\n");
  }
  printf("out\n");
}

void t2() {
  printf("hello t2\n");
  ready2 = true;
  while (!go) {}

  signaled = true;
  Parking::unpark(tid);
  printf("signaled\n");
}


int main(int argc, char** argv) {
  std::vector<std::thread> threads;

  threads.push_back(std::thread(t1));
  threads.push_back(std::thread(t2));

  while (!tid || !ready2) {}
  go = true;
  joinAll(threads);
}
