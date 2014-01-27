#include <euphemus.h>

#include <string.h>
#include <assert.h>

#include "test_schema.h"
#include "test_parse.h"

static void check_test_schema(struct test_schema *test_schema)
{
	assert(eu_string_ref_equal(eu_string_to_ref(&test_schema->str),
				   eu_cstr("x")));
	assert(test_schema->num == 42);
	assert(test_schema->bool);
	assert(eu_value_type(eu_variant_value(&test_schema->any)) == EU_JSON_NULL);
	assert(test_schema->bar);
}

static void test_struct_ptr(void)
{
	TEST_PARSE("{\"str\":\"x\",\"any\":null,\"bar\":{},\"num\":42,\"bool\":true}",
		   struct test_schema *,
		   test_schema_ptr_to_eu_value,
		   check_test_schema(result),
		   test_schema_destroy(result));
}

static void test_inline_struct(void)
{
	TEST_PARSE("{\"str\":\"x\",\"any\":null,\"bar\":{},\"num\":42,\"bool\":true}",
		   struct test_schema,
		   test_schema_to_eu_value,
		   check_test_schema(&result),
		   test_schema_fini(&result));
}

static void test_nested(void)
{
	TEST_PARSE("{\"bar\":{\"bar\":{\"bar\":{\"bar\":{}}}}}",
		   struct test_schema *,
		   test_schema_ptr_to_eu_value,
		   assert(result->bar->bar->bar->bar),
		   test_schema_destroy(result));
}

static void check_extras(struct test_schema *test_schema)
{
	struct eu_value val = eu_value_get_cstr(test_schema_to_eu_value(test_schema), "quux");
	assert(eu_string_ref_equal(eu_value_to_string_ref(val),
				   eu_cstr("foo")));
}

static void test_extras(void)
{
	TEST_PARSE("{\"quux\":\"foo\"}",
		   struct test_schema,
		   test_schema_to_eu_value,
		   check_extras(&result),
		   test_schema_fini(&result));
}

static void test_path(void)
{
	struct test_schema test_schema;
	struct eu_parse ep;
	const char *json = "{\"bar\":{\"bar\":{\"bar\":{\"hello\":\"world\"}}}}";
	struct eu_value val;

	eu_parse_init(&ep, test_schema_to_eu_value(&test_schema));
	assert(eu_parse(&ep, json, strlen(json)));
	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);

	val = test_schema_to_eu_value(&test_schema);
	assert(!eu_value_ok(eu_get_path(val, eu_cstr("/baz"))));
	val = eu_get_path(val, eu_cstr("/bar/bar/bar/hello"));
	assert(eu_value_ok(val));
	assert(eu_value_type(val) == EU_JSON_STRING);
	assert(eu_string_ref_equal(eu_value_to_string_ref(val),
				   eu_cstr("world")));

	test_schema_fini(&test_schema);
}

static void test_path_extras(void)
{
	struct bar bar;
	struct eu_parse ep;
	const char *json = "{\"x\":\"y\"}";
	struct eu_value val;

	eu_parse_init(&ep, bar_to_eu_value(&bar));
	assert(eu_parse(&ep, json, strlen(json)));
	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);

	val = bar_to_eu_value(&bar);
	val = eu_get_path(val, eu_cstr("/x"));
	assert(eu_value_ok(val));
	assert(eu_value_type(val) == EU_JSON_STRING);
	assert(eu_string_ref_equal(eu_value_to_string_ref(val), eu_cstr("y")));

	bar_fini(&bar);
}

static void check_size(const char *json, size_t size)
{
	struct bar bar;
	struct eu_parse ep;

	eu_parse_init(&ep, bar_to_eu_value(&bar));
	assert(eu_parse(&ep, json, strlen(json)));
	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);

	assert(eu_object_size(bar_to_eu_value(&bar)) == size);

	bar_fini(&bar);
}

static void test_size(void)
{
	check_size("{}", 0);
	check_size("{\"x\":\"y\"}", 1);
	check_size("{\"baz\":{}}", 1);
	check_size("{\"baz\":{},\"x\":\"y\"}", 2);
	check_size("{\"str\":\"str\"}", 1);
}

int main(void)
{
	test_struct_ptr();
	test_inline_struct();
	test_nested();
	test_extras();
	test_path();
	test_path_extras();
	test_size();
	return 0;
}
