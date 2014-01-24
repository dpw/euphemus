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
PROJECT_CFLAGS=-I$(ROOT)include -D_GNU_SOURCE -Wno-multichar -Wpointer-arith

# The euphemus library source files
LIB_SRCS=$(foreach S,parse.c path.c struct.c array.c string.c variant.c number.c bool.c null.c,lib/$(S))

# Other source files
SRCS=schemac/schemac.c
SRCS+=$(foreach S,test.c test_codegen.c parse_perf.c test_schema.c,test/$(S))

# Main exectuables that get built
EXECUTABLES=schemac/schemac test/parse_perf

# Test executables that get built
TEST_EXECUTABLES=test/test test/test_codegen

HDROBJS_$(ROOT)include/euphemus.h=$(LIB_SRCS:%.c=%.o)
HDROBJS_$(ROOT)lib/euphemus_int.h=$(LIB_SRCS:%.c=%.o)
HDROBJS_/usr/include/json/json.h=-ljson
HDROBJS_$(ROOT)test_parse.h=

test/test_codegen.o: test/test_schema.h

# Even with .DELETE_ON_ERROR, make will only delete one of the
# targets, hence the 'rm' here.
test/test_schema.c test/test_schema.h: test/test_schema.json schemac/schemac
	schemac/schemac $< || (rm -f test/test_schema.c test/test_schema.h ; false)

HDROBJS_$(ROOT)test/test_schema.h=test/test_schema.o

clean::
	rm -f test/test_schema.c test/test_schema.h

# That completes the definition of the project sources and structure.
# Now for the magic.

ALL_EXECUTABLES:=$(EXECUTABLES) $(TEST_EXECUTABLES)
ALL_SRCS:=$(LIB_SRCS) $(SRCS)

# Disable builtin rules
.SUFFIXES:

.PHONY: all
all: $(ALL_EXECUTABLES)

ifndef MAKECMDGOALS
TESTABLEGOALS:=$(.DEFAULT_GOAL)
else
TESTABLEGOALS:=$(MAKECMDGOALS)
endif

ifneq "$(strip $(patsubst clean%,,$(patsubst %clean,,$(TESTABLEGOALS))))" ""
-include $(foreach S,$(ALL_SRCS),$(S).dep)
endif

.PHONY: clean
clean::
	rm -f $(foreach D,lib test schemac,$(D)/*.o $(D)/*.dep $(D)/*~ $(D)/*.gcda $(D)/*.gcno) $(ALL_EXECUTABLES) coverage/*.gcov

%.o %.c.dep: %.c
	@mkdir -p $(@D)
	$(COMPILE.c) $(PROJECT_CFLAGS) -MD -o $*.o $<
	@sed -e 's|^\([^:]*\):|$*.o $*.c.dep:|' <$*.d >>$*.c.dep
	@sed -e 's/#.*//;s/^[^:]*://;s/ *\\$$//;s/^ *//;/^$$/d;s/$$/ :/' <$*.d >>$*.c.dep
	@sed -e 's/#.*//;s/ [^ ]*\.c//g;s/^\([^ ][^ ]*\):/OBJNEEDS_\1=/;s/\([^ ]*\.h\)/\$$(HDROBJS_\1)/g' <$*.d >>$*.c.dep
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
