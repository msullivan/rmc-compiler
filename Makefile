all: RMC.so

ifeq ($(SMT_SOLVER),CVC4)
DEFINES=-DUSE_CVC4=1
LIBS=-lcvc4
else
DEFINES=-DUSE_Z3=1 -DUSE_Z3_OPTIMIZER=1
LIBS=-lz3

endif

LLVM_LOC=../build/Debug+Asserts/
CXX=clang++
CXXFLAGS = -Wall -Werror -Wno-unused-function $(shell $(LLVM_LOC)/bin/llvm-config --cxxflags | sed s/-fno-exceptions//) $(DEFINES) -g -O0

RMC.o: RMC.cpp RMCInternal.h PathCache.h
PathCache.o: PathCache.cpp PathCache.h
SMTify.o: SMTify.cpp RMCInternal.h PathCache.h smt.h

# For unclear reasons, the Makefile I was cribbing off of had these flags:
# -rdynamic -dylib -flat_namespace
%.so: RMC.o PathCache.o SMTify.o
	$(CXX) -shared $^ -o $@ $(LIBS)
clean:
	rm -f *.o *~ *.so *.bc
