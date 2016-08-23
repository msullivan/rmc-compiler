// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <atomic>
#include <thread>
#include "llvm-cl/CommandLine.h"

#include "util.hpp"

using namespace rmclib;

typedef unsigned long ulong;

const ulong kCount = 10000000;

cl::opt<int> Work("w", cl::desc("Number of dummy work iterations"),
                  cl::init(kDefaultWork));

struct Test {
    const long count;
    Test(int c) : count(c) {}
};

void worker(Test *t) {
    for (int i = 1; i < t->count; i++) {
        fakeWork(Work);
    }
}

cl::opt<bool> BenchMode("b", cl::desc("Use benchmark output"));

void test(Test &t) {
    rmclib::BenchTimer timer;

    worker(&t);

    timer.report(t.count, !BenchMode);
}

cl::opt<int> Count("n", cl::desc("Number of per-thread operations"),
                   cl::init(kCount));
cl::opt<int> Reps("r", cl::desc("Number of times to repeat"), cl::init(1));
cl::opt<int> Dups("d", cl::desc("Number of times to duplicate"), cl::init(1));

int main(int argc, char** argv) {
    cl::ParseCommandLineOptions(argc, argv);

    for (int i = 0; i < Reps; i++) {
        std::vector<std::thread> tests;
        for (int j = 0; j < Dups; j++) {
            tests.push_back(std::thread([=] {
                Test t(Count);
                test(t);
            }));
        }
        joinAll(tests);
    }

    return 0;
}
