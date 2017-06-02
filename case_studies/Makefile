include tests.mk

########

include ../config.mk uppercase.mk

# Files that force rebuilding everything
CONFIG_FILES:=Makefile ../config.mk ../RMC.so uppercase.mk \
	$(wildcard override.mk)

OBJDIR=build

DEFINES :=

ifeq ($(CFG_DISABLE_OPTIMIZE),1)
OPT_LEVEL=-O0
else
OPT_LEVEL=-O2
endif

# Need to specify -march for arm or clang gets mad about barriers??
ifneq (,$(findstring arm,$(shell uname -m)))
ifeq (,$(findstring armv8,$(shell uname -m)))
MARCH:=-march=$(shell uname -m)

# The ARM processors on the Scaleways hosting service are a strange
# beast.  They are ARMv7l but don't support neon and seem to have a
# custom CPU part number that clang doesn't know about. If we see one,
# pass an -mfpu argument to prevent the use of neon.
ifneq (,$(findstring 0x584,$(shell grep "CPU part" /proc/cpuinfo)))
MARCH += -mfpu=vfp3
endif

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
RMC_FLAGS:=$(shell ../rmc-config --cxxflags --cleanup)
CXXFLAGS=--std=c++14 $(RMC_FLAGS) \
	$(MARCH) \
	-Wall -Wno-unused-function $(DEFINES) $(OPT_LEVEL)
DBG_FLAG=-g # Separate flag so we can disable it when we emit .ll
LDFLAGS=-pthreads

# Optional local overrides of any of the above variables
# Especially likely to be useful on ARM where portability is a
# dumpster fire.
-include override.mk

# Generic rules and build stuff
ifdef VERBOSE
  Q :=
  E =
else
  Q := @
  E = echo $(1)
endif

EXTRA_LIB_OBJS=$(patsubst %.cpp, $(OBJDIR)/%.o, $(EXTRA_LIB_SRCS))

$(OBJDIR)/%.o: %.cpp $(CONFIG_FILES)
	@mkdir -p $(dir $@)
	@$(call E, COMPILE $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DBG_FLAG) $(DEP_FLAGS) -c -o $@ $<
$(OBJDIR)/%.ll: %.cpp $(CONFIG_FILES)
	@mkdir -p $(dir $@)
	@$(call E, COMPILE $@)
	$(Q)$(CXX) $(CXXFLAGS) $(DEP_FLAGS) -emit-llvm -S -o $@ $<
$(OBJDIR)/%.filt.ll: $(OBJDIR)/%.ll $(CONFIG_FILES)
	@mkdir -p $(dir $@)
	$(Q)c++filt < $< > $@
$(OBJDIR)/%.filt.s: $(OBJDIR)/%.s $(CONFIG_FILES)
	@mkdir -p $(dir $@)
	$(Q)c++filt < $< > $@

# Args 2-5 define the headers. Abuse the unused arg 1 in
# compile_with_headers to pass the compilation flags.
define compile_with_headers_inner
	@mkdir -p $$(dir $$@)
	@$$(call E, COMPILE $$@)
	$$(Q)$(CXX) $$(CXXFLAGS) $$(DEP_FLAGS)  \
		-D$(call uppercase,$(5))_HEADER='"$(5)_$(2).hpp"' \
		-DEPOCH_HEADER='"epoch_$(3).hpp"' \
		-DFREELIST_HEADER='"freelist_$(4).hpp"' \
		$(1) -o $$@ $$<
endef
# XXX: Arg 1 is unused for no good reason. Args 2-5 define the headers.
define compile_with_headers
	$(call compile_with_headers_inner,$(DBG_FLAG) -c,$(2),$(3),$(4),$(5))
endef
define compile_ll_with_headers
	$(call compile_with_headers_inner,-S -emit-llvm,$(2),$(3),$(4),$(5))
endef
define compile_S_with_headers
	$(call compile_with_headers_inner,-S,$(2),$(3),$(4),$(5))
endef


define TEST_template
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
$$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test.ll: $(1)-test.cpp $$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test.o $$(CONFIG_FILES)
	$(call compile_ll_with_headers,$(1),$(2),$(3),$(4),$(5))
$$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test.s: $(1)-test.cpp $$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test.o $$(CONFIG_FILES)
	$(call compile_S_with_headers,$(1),$(2),$(3),$(4),$(5))

ifeq (,$$(findstring $$(POSSIBLE_LIB_OBJ),$$(BUILTLIBS)))
BUILTLIBS+=$$(POSSIBLE_LIB_OBJ)
$$(POSSIBLE_LIB_OBJ): $(5)_$(2).cpp $$(CONFIG_FILES)
	$(call compile_with_headers,$(1),$(2),$(3),$(4),$(5))
$$(POSSIBLE_LIB_OBJ:.o=.ll): $(5)_$(2).cpp $$(CONFIG_FILES)
	$(call compile_ll_with_headers,$(1),$(2),$(3),$(4),$(5))
$$(POSSIBLE_LIB_OBJ:.o=.s): $(5)_$(2).cpp $$(CONFIG_FILES)
	$(call compile_S_with_headers,$(1),$(2),$(3),$(4),$(5))
endif

$$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test: $$(OBJDIR)/$(1)$$(LIBPART)-$(2)-test.o $$(EXTRA_LIB_OBJS) $$(CONFIG_FILES)
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
	      $(patsubst %.cpp, $(OBJDIR)/%.o, $(LIB_SRCS)) \
	      $(BUILTLIBS)

-include $(patsubst $(OBJDIR)/%.o, $(OBJDIR)/%.d, $(OBJ_FILES))

all: $(TESTS)
.DEFAULT_GOAL := all

########

clean:
	rm -rf *~ *-test $(OBJDIR)
