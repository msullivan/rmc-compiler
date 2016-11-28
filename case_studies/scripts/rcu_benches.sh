#!/bin/bash

N=50

RCU_TESTS=""

RCU_TESTS+="rculist_user-ec11-rmc-test rculist_user-ec11-c11-test rculist_user-ec11-sc-test rculist_user-ec11-linux-test "

# RCU tests where the underlying system matches -- eeeeeeeeh
#RCU_TESTS+="rculist_user-ermc-rmc-test rculist_user-ec11-c11-test rculist_user-ec11-linux-test rculist_user-esc-sc-test "

RCU_TESTS=$(echo $RCU_TESTS | xargs -n1 | sort -u | xargs)

COUNT=3000000
./scripts/bench.sh $N 4x "-p 0 -c 4 -n $COUNT" $RCU_TESTS
./scripts/bench.sh $N 2x "-p 0 -c 2 -n $COUNT" $RCU_TESTS
