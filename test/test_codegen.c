#include <euphemus.h>

#include <string.h>
#include <assert.h>

#include "test_schema.h"
#include "test_parse.h"

static void check_foo(struct foo *foo)
{
	assert(eu_string_ref_equal(eu_string_to_ref(&foo->str), eu_cstr("x")));
	assert(foo->num == 42);
	assert(foo->bool);
	assert(eu_value_type(eu_variant_value(&foo->any)) == EU_JSON_NULL);
	assert(foo->bar);
}

static void test_struct_ptr(void)
{
	TEST_PARSE("{\"str\":\"x\",\"any\":null,\"bar\":{},\"num\":42,\"bool\":true}",
		   struct foo *,
		   foo_ptr_to_eu_value,
		   check_foo(result),
		   foo_destroy(result));
}

static void test_inline_struct(void)
{
	TEST_PARSE("{\"str\":\"x\",\"any\":null,\"bar\":{},\"num\":42,\"bool\":true}",
		   struct foo,
		   foo_to_eu_value,
		   check_foo(&result),
		   foo_fini(&result));
}

static void test_nested(void)
{
	TEST_PARSE("{\"bar\":{\"bar\":{\"bar\":{\"bar\":{}}}}}",
		   struct foo *,
		   foo_ptr_to_eu_value,
		   assert(result->bar->bar->bar->bar),
		   foo_destroy(result));
}

static void check_extras(struct foo *foo)
{
	struct eu_value val = eu_value_get_cstr(foo_to_eu_value(foo), "quux");
	assert(eu_string_ref_equal(eu_value_to_string_ref(val),
				   eu_cstr("foo")));
}

static void test_extras(void)
{
	TEST_PARSE("{\"quux\":\"foo\"}",
		   struct foo,
		   foo_to_eu_value,
		   check_extras(&result),
		   foo_fini(&result));
}

static void test_path(void)
{
	struct foo foo;
	struct eu_parse ep;
	const char *json = "{\"bar\":{\"bar\":{\"bar\":{\"hello\":\"world\"}}}}";
	struct eu_value val;

	eu_parse_init(&ep, foo_to_eu_value(&foo));
	assert(eu_parse(&ep, json, strlen(json)));
	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);

	val = foo_to_eu_value(&foo);
	assert(!eu_value_ok(eu_get_path(val, eu_cstr("/baz"))));
	val = eu_get_path(val, eu_cstr("/bar/bar/bar/hello"));
	assert(eu_value_ok(val));
	assert(eu_value_type(val) == EU_JSON_STRING);
	assert(eu_string_ref_equal(eu_value_to_string_ref(val),
				   eu_cstr("world")));

	foo_fini(&foo);
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
