#!/bin/bash

N=50
COUNT=3000000

RCU_TESTS=""

# RCU tests where we hold the library constant and vary the list impl
#RCU_TESTS+="rculist_user-ec11-rmc-test rculist_user-ec11-c11-test rculist_user-ec11-sc-test rculist_user-ec11-linux-test "

# RCU tests where we hold the list impl constant and vary the library
RCU_TESTS+="rculist_user-esc-linux-test rculist_user-ermc-linux-test rculist_user-ec11-linux-test rculist_user-ec11simp-linux-test "

# RCU tests where the underlying system matches -- eeeeeeeeh
#RCU_TESTS+="rculist_user-ermc-rmc-test rculist_user-ec11-c11-test rculist_user-ec11-linux-test rculist_user-esc-sc-test "

RCU_TESTS=$(echo $RCU_TESTS | xargs -n1 | sort -u | xargs)
echo $RCU_TESTS

./scripts/bench.sh $N 4x "-p 0 -c 4 -n $COUNT" $RCU_TESTS
./scripts/bench.sh $N 2x "-p 0 -c 2 -n $COUNT" $RCU_TESTS
./scripts/bench.sh $N write_heavy_4x "-p 0 -c 4 -n $COUNT -i 30" $RCU_TESTS
./scripts/bench.sh $N write_heavy_2x "-p 0 -c 2 -n $COUNT -i 30" $RCU_TESTS
