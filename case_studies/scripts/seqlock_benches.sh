#!/bin/bash

N=20

SEQLOCK_TESTS=""

SEQLOCK_TESTS+="seqlock-rmc-test seqlock-c11-test "

SEQLOCK_TESTS=$(echo $SEQLOCK_TESTS | xargs -n1 | sort -u | xargs)

COUNT=100000000
./scripts/bench.sh $N 4x "-p 0 -c 4 -n $COUNT" $SEQLOCK_TESTS
./scripts/bench.sh $N 2x "-p 0 -c 2 -n $COUNT" $SEQLOCK_TESTS
