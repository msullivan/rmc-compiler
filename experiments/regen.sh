#!/bin/sh
FLAGS="-Wall -Wextra  -std=c++11 -S -O2"

#CLANG=../../build-opt/bin/clang
CLANG=clang-3.8

set -x
set -e
g++-5 -m32 $FLAGS fragments.cpp -o fragments-gcc-x86_32.s

g++-5 -mcx16 $FLAGS fragments.cpp -o fragments-gcc-x86_64.s

powerpc-linux-gnu-g++-5 $FLAGS fragments.cpp -o fragments-gcc-power.s

arm-linux-gnueabihf-g++-5 -march=armv7-a $FLAGS fragments.cpp -o fragments-gcc-arm.s
arm-linux-gnueabihf-g++-5 -march=armv8-a $FLAGS fragments.cpp -o fragments-gcc-arm8.s

aarch64-linux-gnu-g++-5 $FLAGS fragments.cpp -o fragments-gcc-aarch64.s

LLVM_POWER_FLAGS="--target=powerpc -I /usr/powerpc-linux-gnu/include/ -I /usr/powerpc-linux-gnu/include/c++/5/ -I /usr/powerpc-linux-gnu/include/c++/5/powerpc-linux-gnu"
LLVM_ARM_FLAGS="--target=armv7a -mfloat-abi=hard -I /usr/arm-linux-gnueabihf/include/ -I /usr/arm-linux-gnueabihf/include/c++/5/ -I /usr/arm-linux-gnueabihf/include/c++/5/arm-linux-gnueabihf"
LLVM_ARM8_FLAGS="--target=armv8a -mfloat-abi=hard -I /usr/arm-linux-gnueabihf/include/ -I /usr/arm-linux-gnueabihf/include/c++/5/ -I /usr/arm-linux-gnueabihf/include/c++/5/arm-linux-gnueabihf"
LLVM_AARCH64_FLAGS="--target=aarch64 -I /usr/aarch64-linux-gnu/include/ -I /usr/aarch64-linux-gnu/include/c++/5/ -I /usr/aarch64-linux-gnu/include/c++/5/aarch64-linux-gnu"

$CLANG -m32 $FLAGS fragments.cpp -o fragments-llvm-x86_32.s
$CLANG -mcx16 $FLAGS fragments.cpp -o fragments-llvm-x86_64.s
$CLANG $FLAGS fragments.cpp $LLVM_POWER_FLAGS -o fragments-llvm-power.s
$CLANG $FLAGS fragments.cpp $LLVM_ARM_FLAGS -o fragments-llvm-arm.s
$CLANG $FLAGS fragments.cpp $LLVM_ARM8_FLAGS -o fragments-llvm-arm8.s
$CLANG $FLAGS fragments.cpp $LLVM_AARCH64_FLAGS -o fragments-llvm-aarch64.s

$CLANG -m32 $FLAGS fragments.cpp -o fragments-llvm-x86.ll -emit-llvm
$CLANG $FLAGS fragments.cpp -o fragments-llvm-x86_64.ll -emit-llvm
$CLANG $FLAGS fragments.cpp $LLVM_POWER_FLAGS -o fragments-llvm-power.ll -emit-llvm
$CLANG $FLAGS fragments.cpp $LLVM_ARM_FLAGS -o fragments-llvm-arm.ll -emit-llvm
$CLANG $FLAGS fragments.cpp $LLVM_ARM8_FLAGS -o fragments-llvm-arm8.ll -emit-llvm
$CLANG $FLAGS fragments.cpp $LLVM_AARCH64_FLAGS -o fragments-llvm-aarch64.ll -emit-llvm
