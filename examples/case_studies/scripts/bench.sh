#!/bin/bash

ITERATIONS=$1
shift
PROGRAM=$1
shift
ARGS="$@"

for I in `seq $ITERATIONS`
do
	printf "%d,%s,%s %s,$PROGRAM,$ARGS," $I `hostname` \
		   `date --rfc-3339=seconds -u`
	$PROGRAM -b "$ARGS"
done
