#!/bin/bash

N=20

STACK_TESTS=

# Stack tests where we hold the library constant and vary the queue.
# ... for epoch
STACK_TESTS+="tstack-ec11-rmc-test tstack-ec11-sc-test tstack-ec11-c11-test "
# ... for freelist
STACK_TESTS+="tstack-fc11-rmc2-test tstack-fc11-sc2-test tstack-fc11-c112-test "

# Queue tests where the underlying system matches
# ... for epoch
#STACK_TESTS+="tstack-ermc-rmc-test tstack-esc-sc-test tstack-ec11-c11-test "
# ... for freelist
#STACK_TESTS+="tstack-frmc-rmc2-test tstack-fsc-sc2-test tstack-fc11-c112-test "

# Queue tests where we hold the queue constant and vary the epoch impl
STACK_TESTS+="tstack-ermc-c11-test tstack-esc-c11-test tstack-ec11-c11-test tstack-ec11simp-c11-test "

STACK_TESTS=$(echo $STACK_TESTS | xargs -n1 | sort -u | xargs)
echo $STACK_TESTS

./scripts/bench.sh $N mpmc "-p 2 -c 2" $STACK_TESTS
./scripts/bench.sh $N spsc "-p 1 -c 1" $STACK_TESTS
./scripts/bench.sh $N spmc "-p 1 -c 2" $STACK_TESTS
./scripts/bench.sh $N hammer "-p 0 -c 0 -t 4" $STACK_TESTS
