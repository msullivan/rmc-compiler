CFLAGS=--std=c99 -Wall -Wextra -pthread -O2 $(EXTRA_CFLAGS)
CXXFLAGS=--std=c++11 -Wall -Wextra -pthread -O2 $(EXTRA_CFLAGS)
CC=gcc

all: tests locks

tests: leapfrog-test leapfrog-2-test mp-dep-test mp-imb-test \
       mp-test sb-test wrc-test lb-test sb-syscall-test sb-tmp-test \
       sb-cond-test

%-test: %-test.c test.c atomic.h Makefile
	$(CC) $(CFLAGS) $< test.c -o $@

locks: locks.cpp atomic.h Makefile
	$(CC) $(CXXFLAGS) $< -o $@

clean:
	rm -rf *-test *.o *~ *.bc *.ll
