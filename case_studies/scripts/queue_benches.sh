#!/bin/bash

#TAG=-Xv2
N=50
SIZE=10000000

QUEUE_TESTS=

# Queue tests where we hold the library constant and vary the queue.
# ... for epoch
#QUEUE_TESTS+="ms_queue-ec11-rmc-test ms_queue-ec11-sc-test ms_queue-ec11-c11-test "
# ... for freelist
#QUEUE_TESTS+="ms_queue-fc11-rmc2-test ms_queue-fc11-sc2-test ms_queue-fc11-c112-test "

# Queue tests where the underlying system matches
# ... for epoch
#QUEUE_TESTS+="ms_queue-ermc-rmc-test ms_queue-esc-sc-test ms_queue-ec11-c11-test "
# ... for freelist
#QUEUE_TESTS+="ms_queue-frmc-rmc2-test ms_queue-fsc-sc2-test ms_queue-fc11-c112-test "

# Queue tests where we hold the queue constant and vary the epoch impl
QUEUE_TESTS+="ms_queue-ermc-c11-test ms_queue-esc-c11-test ms_queue-ec11-c11-test ms_queue-ec11simp-c11-test "

QUEUE_TESTS=$(echo $QUEUE_TESTS | xargs -n1 | sort -u | xargs)
echo $QUEUE_TESTS


./scripts/bench.sh $N mpmc${TAG} "-p 2 -c 2 -n ${SIZE}" $QUEUE_TESTS
./scripts/bench.sh $N spsc${TAG} "-p 1 -c 1 -n ${SIZE}" $QUEUE_TESTS
./scripts/bench.sh $N spmc${TAG} "-p 1 -c 2 -n ${SIZE}" $QUEUE_TESTS
./scripts/bench.sh $N hammer${TAG} "-p 0 -c 0 -t 4 -n ${SIZE}" $QUEUE_TESTS
