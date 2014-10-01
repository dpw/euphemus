PROJECT_CFLAGS:=-I$(SROOT)include -D_GNU_SOURCE

# The euphemus library source files
LIB_SRCS=$(addprefix lib/,euphemus.c stack.c parse.c generate.c path.c struct.c \
	array.c string.c variant.c number.c bool.c null.c unescape.c escape.c)

SRCS+=$(LIB_SRCS) schemac/schemac.c schemac/schema_schema.c
SRCS+=$(addprefix test/,test.c test_codegen.c test_schema.c test_common.c)

# Main exectuables that get built
EXECUTABLES=schemac/schemac

# parse_perf requires json-c to build
ifneq "$(wildcard /usr/include/json/json.h)" ""
SRCS+=test/parse_perf.c
EXECUTABLES+=test/parse_perf
HDROBJS_/usr/include/json/json.h:=-ljson
endif

# Test executables that get built
TEST_EXECUTABLES=test/test test/test_codegen

HDROBJS_$(SROOT)include/euphemus.h:=$(LIB_SRCS:%.c=$(ROOT)%.o)
HDROBJS_$(SROOT)lib/euphemus_int.h:=$(LIB_SRCS:%.c=$(ROOT)%.o)
HDROBJS_$(SROOT)lib/unescape.h:=$(ROOT)lib/unescape.o
HDROBJS_$(SROOT)test/test_parse.h:=
HDROBJS_$(SROOT)test/test_common.h:=$(ROOT)test/test_common.o
HDROBJS_$(SROOT)schemac/schema_schema.h:=$(ROOT)schemac/schema_schema.o

$(ROOT)test/test_codegen.o $(ROOT)test/test_codegen.c.dep: $(ROOT)test/test_schema.h

$(foreach E,$(EXECUTABLES) $(TEST_EXECUTABLES),$(eval MAINOBJ_$(ROOT)$(E):=$(ROOT)$(E).o))

# Even with .DELETE_ON_ERROR, make will only delete one of the
# targets, hence the 'rm' here.
$(ROOT)test/test_schema.c $(ROOT)test/test_schema.h: $(ROOT)test/test_schema.json $(ROOT)schemac/schemac
	$(ROOT)schemac/schemac -c $(ROOT)test/test_schema.c -i $(ROOT)test/test_schema.h $< || (rm -f $(ROOT)test/test_schema.c $(ROOT)test/test_schema.h ; false)

# Because this is generated, it starts with HDROBJS_$(ROOT), not HDROBJS_$(SROOT)
HDROBJS_$(ROOT)test/test_schema.h:=$(ROOT)test/test_schema.o

TO_CLEAN+=test/test_schema.c test/test_schema.h
