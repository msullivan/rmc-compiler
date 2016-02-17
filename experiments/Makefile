CFLAGS=--std=c99 -Wall -Wextra -pthread -O2 $(EXTRA_CFLAGS)
CC=gcc

all: tests

tests: leapfrog-test leapfrog-2-test mp-dep-test mp-imb-test \
       mp-test sb-test wrc-test lb-test sb-syscall-test sb-tmp-test

%-test: %-test.c test.c atomic.h Makefile
	$(CC) $(CFLAGS) $< test.c -o $@

clean:
	rm -rf *-test *.o *~ *.bc *.ll
