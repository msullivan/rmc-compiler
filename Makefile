all: RMC.so

LLVM_LOC=../build/Debug+Asserts/
CXX=clang++
CXXFLAGS = -Wall -Werror -Wno-unused-function $(shell $(LLVM_LOC)/bin/llvm-config --cxxflags) -g -O0

RMC.o: RMC.cpp RMCInternal.h PathCache.h
PathCache.o: PathCache.cpp PathCache.h

# For unclear reasons, the Makefile I was cribbing off of had these flags:
# -rdynamic -dylib -flat_namespace
%.so: RMC.o PathCache.o
	$(CXX) -shared $^ -o $@  -lz3
clean:
	rm -f *.o *~ *.so *.bc
