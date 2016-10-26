#!/bin/bash

N=50

RCU_TESTS=""

RCU_TESTS+="rculist_user-ec11-rmc-test rculist_user-ec11-ec11-test "

# RCU tests where the underlying system matches
RCU_TESTS+="rculist_user-ermc-rmc-test rculist_user-ec11-ec11-test "

RCU_TESTS=$(echo $RCU_TESTS | xargs -n1 | sort -u | xargs)

./scripts/bench.sh $N 4x "-p 0 -c 4" $RCU_TESTS
