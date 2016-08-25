#!/bin/bash

ITERATIONS=$1
shift
TAG=$1
shift
# seriously shell scripts are the worst and I should stop
ARGS="$1"
shift
PROGRAMS="$@"

for I in `seq $ITERATIONS`
do
    for PROGRAM in $PROGRAMS
    do
	FILE=data/$PROGRAM-$TAG.csv
	printf "%d,%s,%s %s,$PROGRAM,$ARGS," $I `hostname` \
		   `date --rfc-3339=seconds -u` >> $FILE
	./build/$PROGRAM -b $ARGS >> $FILE
	#sleep 60 # evade, don't solve, temperature problems
    done
done
