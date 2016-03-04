THINGS=ms_queue seqlock
MS_QUEUE_TYPES=sc relacq rmc lock 2lock
SEQLOCK_TYPES=sc
EPOCH_TYPES=sc c11 rmc leak

########

include ../../config.mk uppercase.mk

# Files that force rebuilding everything
CONFIG_FILES=Makefile ../../config.mk ../../RMC.so uppercase.mk

OBJDIR=build

DEFINES :=

ifeq ($(CFG_DISABLE_OPTIMIZE),1)
OPT_LEVEL=-O0
else
OPT_LEVEL=-O2
endif

# Need to specify -march for arm or clang gets mad about barriers??
ifeq (arm,$(findstring arm,$(shell uname -m)))
ifneq (armv8,$(findstring armv8,$(shell uname -m)))
MARCH:=-march=$(shell uname -m)
endif
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

# Compute what tests and stuff we are building
LIB_SRCS=$(patsubst %, epoch_%.cpp, $(EPOCH_TYPES))

TESTS=$(foreach epoch,$(EPOCH_TYPES), \
       $(foreach thing,$(THINGS), \
        $(foreach type,$(value $(call uppercase,$(thing))_TYPES), \
         $(OBJDIR)/$(thing)-e$(epoch)-$(type)-test)))


all: $(TESTS)

CXX:=$(shell $(CFG_LLVM_CONFIG) --bindir)/clang++
CXXFLAGS:=--std=c++14 $(shell ../../rmc-config --cxxflags --smt --cleanup) \
	$(MARCH) \
	-Wall -Wno-unused-function -g $(DEFINES) $(OPT_LEVEL)
LDFLAGS=-pthreads

# Generic rules and build stuff
ifdef VERBOSE
  Q :=
  E =
else
  Q := @
  E = echo $(1)
endif

OBJ_FILES=$(patsubst %, %.o, $(TESTS)) \
	      $(patsubst %.cpp, $(OBJDIR)/%.o, $(LIB_SRCS))

DEP_FLAGS=-MMD -MP -MF $(@:.o=.d) -MT $@
$(OBJDIR)/%.o: %.cpp $(CONFIG_FILES)
	@mkdir -p $(dir $@)
	@$(call E, COMPILE $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DEP_FLAGS) -c -o $@ $<

-include $(patsubst $(OBJDIR)/%.o, $(OBJDIR)/%.d, $(OBJ_FILES))

define TEST_template =
$$(OBJDIR)/$(1)-e$(3)-$(2)-test.o: $(1)-test.cpp $$(CONFIG_FILES)
	@mkdir -p $$(dir $$@)
	@$$(call E, COMPILE $$@)
	$$(Q)$$(CXX) $$(CXXFLAGS) $$(DEP_FLAGS) \
		-D$(call uppercase,$(1))_HEADER='"$(1)_$(2).hpp"' \
		-DEPOCH_HEADER='"epoch_$(3).hpp"' \
		-c -o $$@ $$<
$$(OBJDIR)/$(1)-e$(3)-$(2)-test: $$(OBJDIR)/$(1)-e$(3)-$(2)-test.o $$(OBJDIR)/epoch_$(epoch).o $$(CONFIG_FILES)
	@mkdir -p $$(dir $$@)
	@$$(call E, LINK $$@)
	$$(Q)$$(CXX) -pthread -o $$@ $$(filter %.o,$$^)
endef

########

$(foreach epoch,$(EPOCH_TYPES), \
 $(foreach thing,$(THINGS), \
  $(foreach type,$(value $(call uppercase,$(thing))_TYPES), \
   $(eval $(call TEST_template,$(thing),$(type),$(epoch))))))

########

clean:
	rm -rf *~ *-test $(OBJDIR)
