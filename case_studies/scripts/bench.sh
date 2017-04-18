#!/bin/bash

ITERATIONS=$1
shift
TAG=$1
shift
# seriously shell scripts are the worst and I should stop
ARGS="$1"
shift
PROGRAMS="$@"

# But instead of stopping I am making it worse.
RUN=1
while [ $RUN ]
do
	RUN=
    for PROGRAM in $PROGRAMS
    do
		FILE=data/$PROGRAM-$TAG.csv
		LEN=$(test -a $FILE && wc -l $FILE | cut -f1 -d" " || echo 0)
		if [ $LEN -lt $ITERATIONS ]
		then
			I=$(expr $LEN + 1)
			printf "%d,%s,%s %s,$PROGRAM,$ARGS," $I `hostname` \
				   `date --rfc-3339=seconds -u` >> $FILE
			env -i ./build/$PROGRAM -b $ARGS >> $FILE
			#sleep 60 # evade, don't solve, temperature problems
			RUN=1
		fi
    done
done
