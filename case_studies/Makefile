THINGS=ms_queue seqlock rcu tstack rwlocks qspinlock

MS_QUEUE_TESTS=ms_queue ms_queue-big
MS_QUEUE_EPOCH_TYPES=sc relacq rmc
MS_QUEUE_FREELIST_TYPES=sc2 rmc2
MS_QUEUE_TYPES=lock 2lock

TSTACK_EPOCH_TYPES=sc rmc
TSTACK_FREELIST_TYPES=sc2 rmc2
TSTACK_TYPES=lock

SEQLOCK_TYPES=sc noop c11 rmc

RWLOCKS_TESTS=seqlock-rwlock
RWLOCKS_TYPES=sc c11 rmc

QSPINLOCK_TESTS=seqlock-lock
QSPINLOCK_TYPES=sc

RCU_EPOCH_TYPES=uh

EPOCH_TYPES=sc c11 rmc leak
FREELIST_TYPES=sc rmc leak


#
EXTRA_LIB_SRCS=llvm-cl/CommandLine.cpp

########

include ../config.mk uppercase.mk

# Files that force rebuilding everything
CONFIG_FILES=Makefile ../config.mk ../RMC.so uppercase.mk

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

# -mcx16 is needed to generate cmpxchg16b
ifeq (x86_64,$(findstring x86_64,$(shell uname -m)))
MARCH:=-mcx16
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

DEP_FLAGS=-MMD -MP -MF $(@:.o=.d) -MT $@

CXX:=$(shell $(CFG_LLVM_CONFIG) --bindir)/clang++
CXXFLAGS:=--std=c++14 $(shell ../rmc-config --cxxflags --smt --cleanup) \
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

$(OBJDIR)/%.o: %.cpp $(CONFIG_FILES)
	@mkdir -p $(dir $@)
	@$(call E, COMPILE $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DEP_FLAGS) -c -o $@ $<

# XXX: Arg 1 is unused for no good reason. Args 2-5 define the headers.
define compile_with_headers
	@mkdir -p $$(dir $$@)
	@$$(call E, COMPILE $$@)
	$$(Q)$(CXX) $$(CXXFLAGS) $$(DEP_FLAGS)  \
		-D$(call uppercase,$(5))_HEADER='"$(5)_$(2).hpp"' \
		-DEPOCH_HEADER='"epoch_$(3).hpp"' \
		-DFREELIST_HEADER='"freelist_$(4).hpp"' \
		-c -o $$@ $$<
endef

define TEST_template =
ifneq (none,$(3))
LIBPART:=-e$(3)
$$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test: $$(OBJDIR)/epoch_$(3).o
else ifneq (none,$(4))
LIBPART:=-f$(4)
else
LIBPART:=
endif
POSSIBLE_LIB_OBJ:=$$(OBJDIR)/$(5)_$(2)$$(LIBPART).o

ifneq ($(wildcard $(5)_$(2).cpp),)
$$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test: $$(POSSIBLE_LIB_OBJ)
endif

$$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test.o: $(1)-test.cpp $$(CONFIG_FILES)
	$(call compile_with_headers,$(1),$(2),$(3),$(4),$(5))

ifeq (,$$(findstring $$(POSSIBLE_LIB_OBJ),$$(BUILTLIBS)))
BUILTLIBS+=$$(POSSIBLE_LIB_OBJ)
$$(POSSIBLE_LIB_OBJ): $(5)_$(2).cpp $$(CONFIG_FILES)
	$(call compile_with_headers,$(1),$(2),$(3),$(4),$(5))
endif

$$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test: $$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test.o $$(OBJDIR)/llvm-cl/CommandLine.o $$(CONFIG_FILES)
	@mkdir -p $$(dir $$@)
	@$$(call E, LINK $$@)
	$$(Q)$$(CXX) -pthread -o $$@ $$(filter %.o,$$^)

TESTS += $$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test

endef

########

BUILTLIBS :=
TESTS :=

getdefault = $(if $(1),$(1),$(2))
gettests = $(call getdefault,$(value $(call uppercase,$(1))_TESTS),$(1))

$(foreach thing,$(THINGS), \
 $(foreach test,$(call gettests,$(thing)), \
  $(foreach type,$(value $(call uppercase,$(thing))_EPOCH_TYPES), \
   $(foreach epoch,$(EPOCH_TYPES), \
    $(eval $(call TEST_template,$(test),$(type),$(epoch),none,$(thing)))))))
$(foreach thing,$(THINGS), \
 $(foreach test,$(call gettests,$(thing)), \
  $(foreach type,$(value $(call uppercase,$(thing))_FREELIST_TYPES), \
   $(foreach flist,$(FREELIST_TYPES), \
    $(eval $(call TEST_template,$(test),$(type),none,$(flist),$(thing)))))))
$(foreach thing,$(THINGS), \
 $(foreach test,$(call gettests,$(thing)), \
  $(foreach type,$(value $(call uppercase,$(thing))_TYPES), \
   $(eval $(call TEST_template,$(test),$(type),none,none,$(thing))))))

########

# Compute what tests and stuff we are building
LIB_SRCS=$(patsubst %, epoch_%.cpp, $(EPOCH_TYPES)) \
         $(EXTRA_LIB_SRCS)

OBJ_FILES=$(patsubst %, %.o, $(TESTS)) \
	      $(patsubst %.cpp, $(OBJDIR)/%.o, $(LIB_SRCS))

-include $(patsubst $(OBJDIR)/%.o, $(OBJDIR)/%.d, $(OBJ_FILES))

all: $(TESTS)
.DEFAULT_GOAL := all

########

clean:
	rm -rf *~ *-test $(OBJDIR)
