all: RMC.so

LLVM_LOC=../build/Debug+Asserts/
CXX=clang++
CXXFLAGS = -Wall -Werror -Wno-unused-function $(shell $(LLVM_LOC)/bin/llvm-config --cxxflags) -g -O0

# For unclear reasons, the Makefile I was cribbing off of had these flags:
# -rdynamic -dylib -flat_namespace

%.so: %.o
	$(CXX) -shared $^ -o $@  -lz3
clean:
	rm -f *.o *~ *.so *.bc
