# Make version check
REQUIRED_MAKE_VERSION:=3.81
ifneq ($(shell ( echo "$(MAKE_VERSION)" ; echo "$(REQUIRED_MAKE_VERSION)" ) | sort -t. -n | head -1),$(REQUIRED_MAKE_VERSION))
$(error GNU make version $(REQUIRED_MAKE_VERSION) required)
endif

.DEFAULT_GOAL:=all

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
PROJECT_CFLAGS=-D_GNU_SOURCE -Wno-multichar -Wpointer-arith

# The euphemus library source files
LIB_SRCS=parse.c resolve.c struct.c array.c string.c variant.c number.c bool.c null.c

# Other source files
SRCS=codegen.c test.c test_codegen.c parse_perf.c test_schema.c

# Header files
HDRS=euphemus.h euphemus_int.h test_parse.h

# Main exectuables that get built
EXECUTABLES=codegen parse_perf

# Test executables that get built
TEST_EXECUTABLES=test test_codegen

HDROBJS_$(ROOT)euphemus.h=$(LIB_SRCS:%.c=%.o)
HDROBJS_$(ROOT)euphemus_int.h=$(LIB_SRCS:%.c=%.o)
HDROBJS_/usr/include/json/json.h=-ljson
HDROBJS_$(ROOT)test_parse.h=

test_codegen.o: test_schema.h

# Even with .DELETE_ON_ERROR, make will only delete one of the
# targets, hence the 'rm' here.
test_schema.c test_schema.h: test_schema.json codegen
	./codegen $< || (rm -f test_schema.c test_schema.h ; false)

HDROBJS_$(ROOT)test_schema.h=test_schema.o

clean::
	rm -f test_schema.c test_schema.h

# That completes the definition of the project sources and structure.
# Now for the magic.

ALL_EXECUTABLES:=$(EXECUTABLES) $(TEST_EXECUTABLES)
ALL_SRCS:=$(LIB_SRCS) $(SRCS)
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

%.o $(call dotify,%.c.dep) : %.c
	@mkdir -p $(@D)
	$(COMPILE.c) $(PROJECT_CFLAGS) -MD -o $*.o $<
	@cat $*.d >$(call dotify,$*.c.dep)
	@sed -e 's/#.*//;s/^[^:]*://;s/ *\\$$//;s/^ *//;/^$$/d;s/$$/ :/' <$*.d >>$(call dotify,$*.c.dep)
	@sed -e 's/#.*//;s/ [^ ]*\.c//g;s/^\([^ ][^ ]*\):/OBJNEEDS_\1=/;s/\([^ ]*\.h\)/\$$(HDROBJS_\1)/g' <$*.d >>$(call dotify,$*.c.dep)
	@rm $*.d

# objneeds works out which object files are required to link the given
# object file.
objneeds=$(eval SEEN:=)$(call objneeds_aux,$(1))$(foreach O,$(SEEN),$(eval SAW_$(O):=))$(SEEN)

# objneeds_aux finds the transitive closure of the OBJNEEDS relation,
# starting with $(1), and putting the result in $(SEEN)
objneeds_aux=$(if $(SAW_$(1)),,$(eval SAW_$(1):=1)$(eval SEEN+=$(1))$(if $(filter-out -l%,$(1)),$(foreach O,$(call lookup_undefined,OBJNEEDS_$(1)),$(call objneeds_aux,$(O))),))

# Lookup the object files required to link $(1), returning 'undefined' if it was not defined.
lookup_undefined=$(if $(filter-out undefined,$(flavor $(1))),$($(1)),undefined)

define build_executable
build_executable_objneeds:=$(call objneeds,$(or $(MAINOBJ_$(1)),$(2)$(1).o))
ifeq "$$(filter undefined,$$(build_executable_objneeds))" ""
$(1): $$(build_executable_objneeds)
	$$(CC) $$(CFLAGS) $(PROJECT_CFLAGS) $$^ -o $$@
else
$(1):
	@false
endif
build_executable_objneeds:=
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
