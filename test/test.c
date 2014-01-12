#include <euphemus.h>

#include <string.h>
#include <assert.h>

#include "test_parse.h"

struct eu_array_metadata eu_string_array_metadata
	= EU_ARRAY_METADATA_INITIALIZER(&eu_string_metadata);

struct eu_string_array {
	struct eu_string *a;
	size_t len;
};

void eu_parse_init_string_array(struct eu_parse *ep, struct eu_string_array *a)
{
	eu_parse_init(ep, &eu_string_array_metadata.base, a);
}

void eu_string_array_fini(struct eu_string_array *a)
{
	eu_array_fini(&eu_string_array_metadata.base, a);
}



static void test_parse_string(void)
{
	TEST_PARSE("  \"hello, world!\"  ",
		   struct eu_string,
		   eu_parse_init_string,
		   assert(eu_string_ref_equal(eu_string_to_ref(&result),
					      eu_cstr("hello, world!"))),
		   eu_string_fini(&result));
}

static void test_parse_number(void)
{
	TEST_PARSE("  123456789.0123456789e0  ",
		   double,
		   eu_parse_init_number,
		   assert(result == (double)123456789.0123456789e0),);
	TEST_PARSE("  -0.0123456789e10  ",
		   double,
		   eu_parse_init_number,
		   assert(result == (double)-0.0123456789e10),);
	TEST_PARSE("  0  ",
		   double,
		   eu_parse_init_number,
		   assert(result == 0),);
	TEST_PARSE("  123456789  ",
		   double,
		   eu_parse_init_number,
		   assert(result == (double)123456789),);
	TEST_PARSE("  -123456789  ",
		   double,
		   eu_parse_init_number,
		   assert(result == (double)-123456789),);
	TEST_PARSE("  1000000000000000000000000  ",
		   double,
		   eu_parse_init_number,
		   assert(result == (double)1000000000000000000000000.0),);
}

static void test_parse_bool(void)
{
	TEST_PARSE("  true  ", eu_bool_t, eu_parse_init_bool,
		   assert(result),);
	TEST_PARSE("  false  ", eu_bool_t, eu_parse_init_bool,
		   assert(!result),);
}


static void check_array(struct eu_string_array *a)
{
	assert(a->len == 2);
	assert(eu_string_ref_equal(eu_string_to_ref(&a->a[0]),
				   eu_cstr("foo")));
	assert(eu_string_ref_equal(eu_string_to_ref(&a->a[1]),
				   eu_cstr("bar")));
}

static void test_parse_array(void)
{
	TEST_PARSE("  [  \"foo\"  ,  \"bar\"  ]  ",
		   struct eu_string_array,
		   eu_parse_init_string_array,
		   check_array(&result),
		   eu_string_array_fini(&result));
}


static void check_variant(struct eu_variant *var)
{
	struct eu_value var_val = eu_variant_value(var);
	struct eu_value val;

	assert(eu_value_type(var_val) == EU_JSON_OBJECT);

	assert(eu_value_ok(val = eu_value_get_cstr(var_val, "str")));
	assert(eu_value_type(val) == EU_JSON_STRING);
	assert(eu_string_ref_equal(eu_value_to_string_ref(val),
				   eu_cstr("hello, world!")));

	assert(eu_value_ok(val = eu_value_get_cstr(var_val, "obj")));
	assert(eu_value_type(val) == EU_JSON_OBJECT);
	assert(eu_value_ok(val = eu_value_get_cstr(val, "num")));
	assert(eu_value_type(val) == EU_JSON_NUMBER);
	assert(*eu_value_to_number(val) = 42);

	assert(eu_value_ok(val = eu_value_get_cstr(var_val, "bool")));
	assert(eu_value_type(val) == EU_JSON_BOOL);
	assert(*eu_value_to_bool(val));

	assert(eu_value_ok(val = eu_value_get_cstr(var_val, "null")));
	assert(eu_value_type(val) == EU_JSON_NULL);

	assert(eu_value_ok(val = eu_value_get_cstr(var_val, "array")));
	assert(eu_value_type(val) == EU_JSON_ARRAY);
	assert(eu_value_to_array(val)->len == 2);
}
static void test_parse_variant(void)
{
	TEST_PARSE("  {  \"str\":  \"hello, world!\","
		   "  \"obj\"  :  {  \"num\"  :  42  },"
		   "  \"bool\"  :  true  ,"
		   "  \"null\"  :  null  ,"
		   "  \"array\"  :  [  \"element\"  ,  [  ]  ]  }  ",
		   struct eu_variant,
		   eu_parse_init_variant,
		   check_variant(&result),
		   eu_variant_fini(&result));
}

static void parse_variant(const char *json, struct eu_variant *var)
{
	struct eu_parse ep;

	eu_parse_init_variant(&ep, var);
	assert(eu_parse(&ep, json, strlen(json)));
	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);
}

static void test_path(void)
{
	struct eu_variant var;
	struct eu_value val;

	parse_variant("{\"a\":{\"b\":null,\"c\":true}}", &var);
	val = eu_variant_value(&var);
	assert(!eu_value_ok(eu_get_path(val, eu_cstr("/nosuch"))));
	val = eu_get_path(val, eu_cstr("/a/c"));
	assert(eu_value_ok(val));
	assert(eu_value_type(val) == EU_JSON_BOOL);
	assert(*eu_value_to_bool(val));
	eu_variant_fini(&var);

	parse_variant("[10, 20, 30]", &var);
	val = eu_variant_value(&var);
	val = eu_get_path(val, eu_cstr("/1"));
	assert(eu_value_ok(val));
	assert(eu_value_type(val) == EU_JSON_NUMBER);
	assert(*eu_value_to_number(val) == 20);
	eu_variant_fini(&var);
}

static void check_size(const char *json, size_t size)
{
	struct eu_variant var;

	parse_variant(json, &var);
	assert(eu_object_size(eu_variant_value(&var)) == size);
	eu_variant_fini(&var);
}

static void test_size(void)
{
	check_size("{}", 0);
	check_size("{\"x\":\"y\"}", 1);
	check_size("{\"baz\":{}}", 1);
	check_size("{\"baz\":{},\"x\":\"y\"}", 2);
}

int main(void)
{
	test_parse_string();
	test_parse_number();
	test_parse_bool();
	test_parse_array();
	test_parse_variant();

	test_path();
	test_size();

	return 0;
}
