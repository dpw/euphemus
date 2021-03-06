#include <string.h>

#include <euphemus.h>

#include "test_common.h"
#include "test_schema.h"
#include "test_parse_macro.h"

static void check_test_schema(struct test_schema *test_schema)
{
	require(eu_string_ref_equal(eu_string_to_ref(&test_schema->str),
				   eu_cstr("x")));
	require(test_schema->num == 42.1);
	require(test_schema->int_ == 42);
	require(test_schema->bool);
	require(eu_value_type(eu_variant_value(&test_schema->any)) == EU_JSON_NULL);
	require(test_schema->bar);
	require(test_schema->array.len == 1);
	require(eu_string_ref_equal(eu_string_to_ref(&test_schema->array.a[0].str),
				   eu_cstr("y")));
}

static char test_schema_json[] = "{\"str\":\"x\",\"num\":42.1,\"int_\":42,\"bool\":true,\"any\":null,\"bar\":{},\"array\":[{\"str\":\"y\"}]}";

static void test_struct_ptr(void)
{
	TEST_PARSE(test_schema_json,
		   struct test_schema *,
		   test_schema_ptr_to_eu_value,
		   check_test_schema(result),
		   test_schema_destroy(result));
}

static void test_bad_struct_ptr(void)
{
	struct test_schema *result;
	struct eu_parse *parse
		= eu_parse_create(test_schema_ptr_to_eu_value(&result));
	char *json = "{\"str\":\"\\wrong\"}";
	size_t len = strlen(json);
	size_t i;

	require(!eu_parse(parse, json, len));
	eu_parse_destroy(parse);
	test_schema_destroy(result);

	parse = eu_parse_create(test_schema_ptr_to_eu_value(&result));
	for (i = 0;; i++) {
		char c;

		require(i < len);
		c = json[i];
		if (!eu_parse(parse, &c, 1))
			break;
	}

	eu_parse_destroy(parse);
	test_schema_destroy(result);
}

static void test_inline_struct(void)
{
	TEST_PARSE(test_schema_json,
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
		   require(result->bar->bar->bar->bar),
		   test_schema_destroy(result));
}

static void check_extras(struct test_schema *test_schema)
{
	struct eu_value val = eu_value_get_cstr(test_schema_to_eu_value(test_schema), "quux");
	require(eu_string_ref_equal(eu_value_to_string_ref(val),
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
	struct eu_parse *parse;
	const char *json = "{\"bar\":{\"bar\":{\"bar\":{\"hello\":\"world\"}}},\"array\":[{\"str\":\"x\"},{\"str\":\"y\"},{\"str\":\"z\"}]}";
	struct eu_value val;
	struct bar *p;

	parse = eu_parse_create(test_schema_to_eu_value(&test_schema));
	require(eu_parse(parse, json, strlen(json)));
	require(eu_parse_finish(parse));
	eu_parse_destroy(parse);

	val = test_schema_to_eu_value(&test_schema);
	require(!eu_value_ok(eu_get_path(val, eu_cstr("/baz"))));
	val = eu_get_path(val, eu_cstr("/bar/bar/bar/hello"));
	require(eu_value_type(val) == EU_JSON_STRING);
	require(eu_string_ref_equal(eu_value_to_string_ref(val),
				   eu_cstr("world")));

	val = test_schema_to_eu_value(&test_schema);
	val = eu_get_path(val, eu_cstr("/array/1"));
	require(eu_value_type(val) == EU_JSON_OBJECT);
	require(val.metadata == struct_bar_metadata());
	p = val.value;
	require(eu_string_ref_equal(eu_string_to_ref(&p->str), eu_cstr("y")));

	test_schema_fini(&test_schema);
}

static void test_path_extras(void)
{
	struct bar bar;
	struct eu_parse *parse;
	const char *json = "{\"x\":\"y\"}";
	struct eu_value val;

	parse = eu_parse_create(bar_to_eu_value(&bar));
	require(eu_parse(parse, json, strlen(json)));
	require(eu_parse_finish(parse));
	eu_parse_destroy(parse);

	val = bar_to_eu_value(&bar);
	val = eu_get_path(val, eu_cstr("/x"));
	require(eu_value_type(val) == EU_JSON_STRING);
	require(eu_string_ref_equal(eu_value_to_string_ref(val), eu_cstr("y")));

	bar_fini(&bar);
}

static void check_size(const char *json, size_t size)
{
	struct bar bar;
	struct eu_parse *parse;

	parse = eu_parse_create(bar_to_eu_value(&bar));
	require(eu_parse(parse, json, strlen(json)));
	require(eu_parse_finish(parse));
	eu_parse_destroy(parse);

	require(eu_object_size(bar_to_eu_value(&bar)) == size);

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

static void test_gen_struct(void)
{
	struct test_schema ts;

	test_schema_init(&ts);
	test_gen(test_schema_to_eu_value(&ts), eu_cstr("{}"));

	eu_string_assign_empty(&ts.str);
	test_gen(test_schema_to_eu_value(&ts), eu_cstr("{\"str\":\"\"}"));

	require(eu_string_assign(&ts.str, eu_cstr("hello")));
	test_gen(test_schema_to_eu_value(&ts), eu_cstr("{\"str\":\"hello\"}"));
	eu_string_reset(&ts.str);

	test_schema_set_num(&ts, 42.1);
	test_gen(test_schema_to_eu_value(&ts), eu_cstr("{\"num\":42.1}"));
	test_schema_set_num_present(&ts, 0);

	test_schema_set_int_(&ts, 42);
	test_gen(test_schema_to_eu_value(&ts), eu_cstr("{\"int_\":42}"));
	test_schema_set_int__present(&ts, 0);

	eu_variant_assign_number(&ts.any, 123);
	test_gen(test_schema_to_eu_value(&ts), eu_cstr("{\"any\":123}"));

	eu_variant_reset(&ts.any);
	require(struct_bar_array_push(&ts.array));
	test_gen(test_schema_to_eu_value(&ts), eu_cstr("{\"array\":[{}]}"));

	test_schema_fini(&ts);
}

static void test_gen_parsed_struct(void)
{
	struct eu_string_ref json = eu_cstr(test_schema_json);
	struct test_schema ts;
	struct eu_parse *parse;

	require(parse = eu_parse_create(test_schema_to_eu_value(&ts)));
	require(eu_parse(parse, json.chars, json.len));
	require(eu_parse_finish(parse));
	eu_parse_destroy(parse);

	test_gen(test_schema_to_eu_value(&ts), json);
	test_schema_fini(&ts);
}

static void test_escaped_member_names(void)
{
	struct eu_string_ref json = eu_cstr("{\"hello \\\"\\u0395\\u1f54\\u03c6\\u03b7\\u03bc\\u03bf\\u03c2\\\"\":true}");
	struct test_schema ts;
	struct eu_parse *parse;

	require(parse = eu_parse_create(test_schema_to_eu_value(&ts)));
	require(eu_parse(parse, json.chars, json.len));
	require(eu_parse_finish(parse));
	eu_parse_destroy(parse);

	test_gen(test_schema_to_eu_value(&ts),
		 eu_cstr("{\"hello \\\"\316\225\341\275\224\317\206\316\267\316\274\316\277\317\202\\\"\":true}"));
	test_schema_fini(&ts);
}

static void test_int(struct eu_string_ref json, eu_integer_t i)
{
	struct test_schema ts;
	struct eu_parse *parse;

	require(parse = eu_parse_create(test_schema_to_eu_value(&ts)));
	require(eu_parse(parse, json.chars, json.len));
	require(eu_parse_finish(parse));
	eu_parse_destroy(parse);
	require(ts.int_ == i);

	test_gen(test_schema_to_eu_value(&ts), json);
	test_schema_fini(&ts);
}

static void test_int_overflow(struct eu_string_ref json)
{
	struct test_schema ts;
	struct eu_parse *parse;

	require(parse = eu_parse_create(test_schema_to_eu_value(&ts)));
	require(!eu_parse(parse, json.chars, json.len));
	eu_parse_destroy(parse);
	test_schema_fini(&ts);
}

static void test_big_ints(void)
{
	test_int(eu_cstr("{\"int_\":9223372036854775806}"),
		 9223372036854775806);
	test_int(eu_cstr("{\"int_\":9223372036854775807}"),
		 9223372036854775807);
	test_int_overflow(eu_cstr("{\"int_\":9223372036854775808}"));

	test_int(eu_cstr("{\"int_\":-9223372036854775807}"),
		 -9223372036854775807);
	test_int(eu_cstr("{\"int_\":-9223372036854775808}"),
		 -9223372036854775807-1);
	test_int_overflow(eu_cstr("{\"int_\":-9223372036854775809}"));
}

static void test_bad_int(struct eu_string_ref json)
{
	struct test_schema ts;
	struct eu_parse *parse;

	require(parse = eu_parse_create(test_schema_to_eu_value(&ts)));
	require(!eu_parse(parse, json.chars, json.len));
	eu_parse_destroy(parse);
	test_schema_fini(&ts);
}

static void test_bad_ints(void)
{
	test_bad_int(eu_cstr("{\"int_\":0.5}"));
	test_bad_int(eu_cstr("{\"int_\":1e3}"));
}

int main(void)
{
	test_struct_ptr();
	test_bad_struct_ptr();
	test_inline_struct();
	test_nested();
	test_extras();
	test_path();
	test_path_extras();
	test_size();
	test_gen_struct();
	test_gen_parsed_struct();
	test_escaped_member_names();
	test_big_ints();
	test_bad_ints();
	return 0;
}
