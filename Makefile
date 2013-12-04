# Make version check
REQUIRED_MAKE_VERSION:=3.81
ifneq ($(shell ( echo "$(MAKE_VERSION)" ; echo "$(REQUIRED_MAKE_VERSION)" ) | sort -t. -n | head -1),$(REQUIRED_MAKE_VERSION))
$(error GNU make version $(REQUIRED_MAKE_VERSION) required)
endif

MAKEFILE:=$(firstword $(MAKEFILE_LIST))

# You can override this from the command line
CFLAGS=-Wall -Wextra -ansi -g

# ROOT is the path to the source tree.  If non-empty, then it includes
# a trailing slash.
ROOT=$(filter-out ./,$(dir $(MAKEFILE)))

# VPATH tells make where to search for sources, if buliding from a
# separate build tree.
VPATH=$(ROOT)

# It's less likely that you'll want to override this
PROJECT_CFLAGS=

# Principal source files
SRCS=parse.c struct.c array.c string.c variant.c number.c bool.c null.c

# Test source files
TEST_SRCS=test.c test_parse.c

# Header files
HDRS=euphemus.h euphemus_int.h

# Main exectuables that get built
EXECUTABLES=test_parse

# Test executables that get built
TEST_EXECUTABLES=test

HDROBJS_$(ROOT)euphemus.h=$(SRCS:%.c=%.o)
HDROBJS_$(ROOT)euphemus_int.h=$(SRCS:%.c=%.o)

# That completes the definition of the project sources and structure.
# Now for the magic.

ALL_EXECUTABLES:=$(EXECUTABLES) $(TEST_EXECUTABLES)
ALL_SRCS:=$(SRCS) $(TEST_SRCS)
$(foreach H,$(HDRS),$(eval HDROBJS_$(ROOT)$(H)?=$(notdir $(H:%.h=%.o))))

# Disable builtin rules
.SUFFIXES:

.PHONY: all
all: $(ALL_EXECUTABLES)

ifndef MAKECMDGOALS
TESTABLEGOALS:=$(.DEFAULT_GOAL)
else
TESTABLEGOALS:=$(MAKECMDGOALS)
endif

# dotify puts a dot in front of the given filename, respecting any
# directories that may be in the path.
dotify=$(call fileprefix,.,$(1))
fileprefix=$(foreach F,$(2),$(if $(filter $(notdir $(F)),$(F)),$(1)$(F),$(dir $(F))$(1)$(notdir $(F))))

ifneq "$(strip $(patsubst clean%,,$(patsubst %clean,,$(TESTABLEGOALS))))" ""
-include $(foreach S,$(ALL_SRCS),$(call dotify,$(S).dep))
endif

.PHONY: clean
clean::
	rm -f *.o .*.dep *~ *.gcda *.gcno $(ALL_EXECUTABLES) coverage/*.gcov

ifneq ($(ROOT),)
root_frob=;s|$(ROOT)|\#|g
root_unfrob=;s|\#|$(ROOT)|g
endif

%.o $(call dotify,%.c.dep) : %.c
	@mkdir -p $(@D)
	$(COMPILE.c) $(PROJECT_CFLAGS) -MD -o $*.o $<
	@cat $*.d >$(call dotify,$*.c.dep)
	@sed -e 's/#.*//;s/^[^:]*://;s/ *\\$$//;s/^ *//;/^$$/d;s/$$/ :/' <$*.d >>$(call dotify,$*.c.dep)
	@sed -e 's/#.*//;s/ [^ ]*\.c//$(root_frob);s| /[^ ]*||g;/^ *\\$$/d$(root_unfrob);s/^\([^ ][^ ]*\):/OBJNEEDS_\1=/;s/\([^ ]*\.h\)/\$$(HDROBJS_\1)/g' <$*.d >>$(call dotify,$*.c.dep)
	@rm $*.d

# objneeds works out which object files are required to link the given
# object file.
objneeds=$(eval SEEN:=)$(call objneeds_aux,$(1))$(foreach O,$(SEEN),$(eval SAW_$(O):=))$(SEEN)
objneeds_aux=$(if $(SAW_$(1)),,$(eval SAW_$(1):=1)$(eval SEEN+=$(1))$(foreach O,$(OBJNEEDS_$(1)),$(call objneeds_aux,$(O))))

define build_executable
$(1): $(call objneeds,$(or $(MAINOBJ_$(1)),$(2)$(1).o))
	$$(CC) $$(CFLAGS) $(PROJECT_CFLAGS) $$^ -o $$@
endef

$(foreach E,$(EXECUTABLES),$(eval $(call build_executable,$(E),)))
$(foreach E,$(TEST_EXECUTABLES),$(eval $(call build_executable,$(E),)))

.PHONY: run_tests
run_tests: $(TEST_EXECUTABLES)
	$(foreach T,$(TEST_EXECUTABLES),./$(T) &&) :

.PHONY: coverage
coverage:
	$(MAKE) -f $(MAKEFILE) clean
	$(MAKE) -f $(MAKEFILE) all CFLAGS="$(CFLAGS) --coverage"
	$(foreach T,$(TEST_EXECUTABLES),./$(T) &&) :
	mkdir -p coverage
	gcov -p $(ALL_SRCS)
	mv *.gcov coverage
