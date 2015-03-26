#!/bin/sh
FLAGS="-Wall -Wextra  -std=c++11 -S -O2"

set -x
g++-4.9 $FLAGS fragments.cpp -o fragments-gcc-x86.s

powerpc-linux-gnu-g++-4.9 $FLAGS fragments.cpp -o fragments-gcc-power.s

arm-linux-gnueabihf-g++-4.9 -march=armv7-a $FLAGS fragments.cpp -o fragments-gcc-arm.s
arm-linux-gnueabihf-g++-4.9 -march=armv8-a $FLAGS fragments.cpp -o fragments-gcc-arm8.s

aarch64-linux-gnu-g++-4.9 $FLAGS fragments.cpp -o fragments-gcc-aarch64.s

LLVM_POWER_FLAGS="--target=powerpc -I /usr/powerpc-linux-gnu/include/ -I /usr/powerpc-linux-gnu/include/c++/4.9.1/ -I /usr/powerpc-linux-gnu/include/c++/4.9.1/powerpc-linux-gnu"
LLVM_ARM_FLAGS="--target=armv7a -mfloat-abi=hard -I /usr/arm-linux-gnueabihf/include/ -I /usr/arm-linux-gnueabihf/include/c++/4.9.1/ -I /usr/arm-linux-gnueabihf/include/c++/4.9.1/arm-linux-gnueabihf"
LLVM_ARM8_FLAGS="--target=armv8a -mfloat-abi=hard -I /usr/arm-linux-gnueabihf/include/ -I /usr/arm-linux-gnueabihf/include/c++/4.9.1/ -I /usr/arm-linux-gnueabihf/include/c++/4.9.1/arm-linux-gnueabihf"
LLVM_AARCH64_FLAGS="--target=aarch64 -I /usr/aarch64-linux-gnu/include/ -I /usr/aarch64-linux-gnu/include/c++/4.9.1/ -I /usr/aarch64-linux-gnu/include/c++/4.9.1/aarch64-linux-gnu"

../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp -o fragments-llvm-x86.s
../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp $LLVM_POWER_FLAGS -o fragments-llvm-power.s
../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp $LLVM_ARM_FLAGS -o fragments-llvm-arm.s
../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp $LLVM_ARM8_FLAGS -o fragments-llvm-arm8.s
../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp $LLVM_AARCH64_FLAGS -o fragments-llvm-aarch64.s

../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp -o fragments-llvm-x86.ll -emit-llvm
../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp $LLVM_POWER_FLAGS -o fragments-llvm-power.ll -emit-llvm
../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp $LLVM_ARM_FLAGS -o fragments-llvm-arm.ll -emit-llvm
../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp $LLVM_ARM8_FLAGS -o fragments-llvm-arm8.ll -emit-llvm
../../build-opt/Release+Asserts/bin/clang $FLAGS fragments.cpp $LLVM_AARCH64_FLAGS -o fragments-llvm-aarch64.ll -emit-llvm
