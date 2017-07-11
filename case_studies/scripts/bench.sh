#!/bin/bash

ITERATIONS=$1
shift
TAG=$1
shift
# seriously shell scripts are the worst and I should stop
ARGS="$1"
shift
PROGRAMS="$@"

# Find out which programs actually need more runs done
NPROGRAMS=""
for PROGRAM in $PROGRAMS
do
	FILE=data/$PROGRAM-$TAG.csv
	LEN=$(test -a $FILE && wc -l $FILE | cut -f1 -d" " || echo 0)
	if [ $LEN -lt $ITERATIONS ]
	then
		NPROGRAMS+="build/$PROGRAM "
	fi
done

# If there are any programs that need to be run, rebuild the compiler,
# then rebuild the programs. Otherwise we are done.
if [ ! "$NPROGRAMS" ]; then exit; fi
make -j4 -C ..
make -j4 $NPROGRAMS

printf "\n\nRunning $TAG for $NPROGRAMS\n"

# But instead of stopping I am making it worse.
RUN=1
while [ $RUN ]
do
	RUN=
    for PROGRAM in $PROGRAMS
    do
		# sigh, code duplication
		FILE=data/$PROGRAM-$TAG.csv
		LEN=$(test -a $FILE && wc -l $FILE | cut -f1 -d" " || echo 0)
		if [ $LEN -lt $ITERATIONS ]
		then
			I=$(expr $LEN + 1)
			(printf "%d,%s,%s %s,$PROGRAM,$ARGS," $I `hostname` \
				   `date --rfc-3339=seconds -u`;
			 env -i ./build/$PROGRAM -b $ARGS) | tee -a $FILE
			#sleep 60 # evade, don't solve, temperature problems
			RUN=1
		fi
    done
done
