SRCS=RMC.cpp PathCache.cpp SMTify.cpp

all: RMC.so

# Files that force rebuilding everything
CONFIG_FILES=Makefile

LLVM_LOC=../build/Debug+Asserts/

OBJDIR=build

ifeq ($(SMT_SOLVER),CVC4)
DEFINES=-DUSE_CVC4=1
LIBS=-lcvc4
else
DEFINES=-DUSE_Z3=1 -DUSE_Z3_OPTIMIZER=1
LIBS=-lz3
endif

OPT_LEVEL=-O0
CXX=clang++

LLVM_FLAGS:=$(shell $(LLVM_LOC)/bin/llvm-config --cxxflags | sed s/-fno-exceptions//)
CXXFLAGS=-Wall -Wno-unused-function $(LLVM_FLAGS) $(DEFINES) -g $(OPT_LEVEL)


# Generic rules and build stuff
OBJ_FILES=$(patsubst %.cpp, $(OBJDIR)/%.o, $(SRCS))

DEP_FLAGS=-MD -MP -MF $(@:.o=.d) -MT $@
$(OBJDIR)/%.o: %.cpp $(CONFIG_FILES)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEP_FLAGS) -c -o $@ $<

-include $(patsubst %.cpp, $(OBJDIR)/%.d, $(SRCS))

RMC.so: $(OBJ_FILES) $(CONFIG_FILES)
	$(CXX) -shared $(OBJ_FILES) -o $@ $(LIBS)

clean:
	rm -rf *~ *.so $(OBJDIR)
