THINGS=ms_queue
MS_QUEUE_TYPES=sc rmc lock 2lock

LIB_SRCS=epoch_sc.cpp

########

include ../../config.mk uppercase.mk

# Files that force rebuilding everything
CONFIG_FILES=Makefile ../../config.mk uppercase.mk

#OBJDIR=build
OBJDIR=.

DEFINES :=

ifeq ($(CFG_DISABLE_OPTIMIZE),1)
OPT_LEVEL=-O0
else
OPT_LEVEL=-O2
endif

# Need to specify -march for arm or clang gets mad about barriers??
ifeq (arm,$(findstring arm,$(shell uname -m)))
MARCH:=-march=$(shell uname -m)
endif

# Clang and gcc disagree for -march=armv6 on whether to define
# __GCC_HAVE_SYNC_COMPARE_AND_SWAP_2. The libstdc++ ABI for shared_ptr
# depends on this choice. So check to see the system compiler defines
# it or not, and if it doesn't, manually unset it.
# asdf.
MISSING_CAS2:=$(shell cc -dM -E - < /dev/null | \
                grep -q __GCC_HAVE_SYNC_COMPARE_AND_SWAP_2 || echo 1)
ifeq (1,$(MISSING_CAS2))
DEFINES += -U__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2
endif

# Compute what tests we are building
TESTS=$(foreach thing,$(THINGS), \
        $(foreach type,$(value $(call uppercase,$(thing))_TYPES), \
         $(thing)-esc-$(type)-test))


all: $(TESTS)

# Generic rules and build stuff
CXX:=$(shell $(CFG_LLVM_CONFIG) --bindir)/clang++
CXXFLAGS:=--std=c++14 $(shell ../../rmc-config --cxxflags --use-smt) \
	$(MARCH) \
	-Wall -Wno-unused-function -g $(DEFINES) $(OPT_LEVEL)
LDFLAGS=-pthreads


OBJ_FILES=$(patsubst %, $(OBJDIR)/%.o, $(TESTS)) \
	      $(patsubst %.cpp, $(OBJDIR)/%.o, $(LIB_SRCS))

DEP_FLAGS=-MMD -MP -MF $(@:.o=.d) -MT $@
$(OBJDIR)/%.o: %.cpp $(CONFIG_FILES)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEP_FLAGS) -c -o $@ $<

-include $(patsubst $(OBJDIR)/%.o, $(OBJDIR)/%.d, $(OBJ_FILES))

define TEST_template =
$$(OBJDIR)/$(1)-esc-$(2)-test.o: $(1)-test.cpp $$(CONFIG_FILES)
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(CXXFLAGS) $$(DEP_FLAGS) \
		-DUSE_$(call uppercase,$(2))_$(call uppercase,$(1))=1 -c \
		-o $$@ $$<
$(1)-esc-$(2)-test: $$(OBJDIR)/$(1)-esc-$(2)-test.o $$(OBJDIR)/epoch_sc.o $$(CONFIG_FILES)
	@mkdir -p $$(dir $$@)
	$$(CXX) -pthread -o $$@ $$(filter %.o,$$^)
endef

########

$(foreach thing,$(THINGS), \
 $(foreach type,$(value $(call uppercase,$(thing))_TYPES), \
  $(eval $(call TEST_template,$(thing),$(type)))))

########

clean:
	rm -rf *~ *.o *.d *-test
