#!/bin/sh
FLAGS="-Wall -Wextra  -std=c++11 -S -O2"

set -x
powerpc-linux-gnu-g++-4.8 $FLAGS fragments.cpp -o fragments-gcc-power.s

arm-linux-gnueabihf-g++-4.8 $FLAGS fragments.cpp -o fragments-gcc-arm.s

../../build-opt/Release+Asserts/bin/clang $FLAGS --target=powerpc fragments.cpp -I /usr/lib/gcc-cross/powerpc-linux-gnu/4.8/include -I /usr/powerpc-linux-gnu/include/c++/4.8.2 -I /usr/powerpc-linux-gnu/include/c++/4.8.2/powerpc-linux-gnu -o fragments-llvm-power.s

../../build-opt/Release+Asserts/bin/clang $FLAGS --target=arm -mfloat-abi=soft fragments.cpp -I /usr/arm-linux-gnueabihf/include/c++/4.8.2/ -I /usr/arm-linux-gnueabihf/include/c++/4.8.2/arm-linux-gnueabihf/ -o fragments-llvm-arm.s
