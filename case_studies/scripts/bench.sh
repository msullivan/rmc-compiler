#!/bin/bash

get_temp () {
	read_temp 2>/dev/null || echo 0
}

get_len() {
	test -a $1 && wc -l $1 | cut -f1 -d" " || echo 0
}

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
	LEN=$(get_len $FILE)
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
# RUN=1
# while [ $RUN ]
# do
# 	RUN=
#     for PROGRAM in $PROGRAMS
#     do
# 		FILE=data/$PROGRAM-$TAG.csv
# 		LEN=$(get_len $FILE)
# 		if [ $LEN -lt $ITERATIONS ]
# 		then
# 			I=$(expr $LEN + 1)
# 			printf "%d,%s,%s %s,$PROGRAM,$ARGS,%s,%d\n" $I $(hostname) \
# 				   $(date --rfc-3339=seconds -u) \
# 				   $(env -i ./build/$PROGRAM -b $ARGS) \
# 				   $(get_temp) | tee -a $FILE
# 			#sleep 60 # evade, don't solve, temperature problems
# 			RUN=1
# 		fi
#     done
# done

for PROGRAM in $PROGRAMS
do
	FILE=data/$PROGRAM-$TAG.csv
	LEN=$(get_len $FILE)
	if [ $LEN -lt $ITERATIONS ]
	then
		printf "Warmup run: %s\n" $(env -i ./build/$PROGRAM -b $ARGS)
		for I in $(seq $(expr $LEN + 1) $ITERATIONS)
		do
			printf "%d,%s,%s %s,$PROGRAM,$ARGS,%s,%d\n" $I $(hostname) \
				   $(date --rfc-3339=seconds -u) \
				   $(env -i ./build/$PROGRAM -b $ARGS) \
				   $(get_temp) | tee -a $FILE
			#sleep 60 # evade, don't solve, temperature problems
		done
	fi
done
