TESTS=ms_queue-esc-sc-test ms_queue-esc-rmc-test
LIB_SRCS=epoch_sc.cpp

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

all: $(TESTS)

# Generic rules and build stuff
CXX=$(shell $(CFG_LLVM_CONFIG) --bindir)/clang++
CXXFLAGS=--std=c++14 $(shell ../../rmc-config --cxxflags --use-smt) \
	-march=$(shell uname -m) \
	-Wall -Wno-unused-function -g $(DEFINES) $(OPT_LEVEL)
LDFLAGS=-pthreads


OBJ_FILES=$(patsubst %-test, $(OBJDIR)/%.o, $(TESTS)) \
	      $(patsubst %.cpp, $(OBJDIR)/%.o, $(LIB_SRCS))

DEP_FLAGS=-MMD -MP -MF $(@:.o=.d) -MT $@
$(OBJDIR)/%.o: %.cpp $(CONFIG_FILES)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEP_FLAGS) -c -o $@ $<

-include $(patsubst %.o, $(OBJDIR)/%.d, $(OBJ_FILES))

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

$(eval $(call TEST_template,ms_queue,sc))
$(eval $(call TEST_template,ms_queue,rmc))

clean:
	rm -rf *~ *.o *.d *-test
