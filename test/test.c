#include <string.h>
#include <assert.h>

#include <euphemus.h>

#include "test_common.h"
#include "test_parse.h"

static void test_parse_string(void)
{
	TEST_PARSE("  \"hello, world!\"  ",
		   struct eu_string,
		   eu_string_value,
		   assert(eu_string_ref_equal(eu_string_to_ref(&result),
					      eu_cstr("hello, world!"))),
		   eu_string_fini(&result));
	TEST_PARSE("  \"\\\" \\/ \\b \\f \\n \\r \\t \\u0041 \\u03Bb \\u2f08 \\\\\"  ",
		   struct eu_string,
		   eu_string_value,
		   assert(eu_string_ref_equal(eu_string_to_ref(&result),
		    eu_cstr("\" / \b \f \n \r \t A \316\273 \342\274\210 \\"))),
		   eu_string_fini(&result));
}

static void test_parse_number(void)
{
	TEST_PARSE("  123456789.0123456789e0  ",
		   eu_number_t,
		   eu_number_value,
		   assert(result == (eu_number_t)123456789.0123456789e0),);
	TEST_PARSE("  -0.0123456789e-10  ",
		   eu_number_t,
		   eu_number_value,
		   assert(result == (eu_number_t)-0.0123456789e-10),);
	TEST_PARSE("  0  ",
		   eu_number_t,
		   eu_number_value,
		   assert(result == 0),);
	TEST_PARSE("  123456789  ",
		   eu_number_t,
		   eu_number_value,
		   assert(result == (eu_number_t)123456789),);
	TEST_PARSE("  -123456789  ",
		   eu_number_t,
		   eu_number_value,
		   assert(result == (eu_number_t)-123456789),);
	TEST_PARSE("  1000000000000000000000000  ",
		   eu_number_t,
		   eu_number_value,
		   assert(result == (eu_number_t)1000000000000000000000000.0),);
}

static void test_parse_number_truncated(void)
{
	struct eu_parse *parse;
	eu_number_t result;

	parse = eu_parse_create(eu_number_value(&result));
	assert(eu_parse(parse, "1234.567", 6));
	assert(eu_parse_finish(parse));
	eu_parse_destroy(parse);
	assert(result == 1234.5);
}


static void test_parse_bool(void)
{
	TEST_PARSE("  true  ", eu_bool_t, eu_bool_value,
		   assert(result),);
	TEST_PARSE("  false  ", eu_bool_t, eu_bool_value,
		   assert(!result),);
}


static void check_variant(struct eu_variant *var)
{
	struct eu_value var_val = eu_variant_value(var);
	struct eu_value val;

	assert(eu_value_type(var_val) == EU_JSON_OBJECT);

	val = eu_value_get_cstr(var_val, "str\\\"");
	assert(eu_value_type(val) == EU_JSON_STRING);
	assert(eu_string_ref_equal(eu_value_to_string_ref(val),
				   eu_cstr("hello, world!")));

	val = eu_value_get_cstr(var_val, "obj");
	assert(eu_value_type(val) == EU_JSON_OBJECT);
	val = eu_value_get_cstr(val, "num");
	assert(eu_value_type(val) == EU_JSON_NUMBER);
	assert(eu_value_to_double(val).ok);
	assert(eu_value_to_double(val).value == 4.2);

	val = eu_value_get_cstr(var_val, "bool");
	assert(eu_value_type(val) == EU_JSON_BOOL);
	assert(*eu_value_to_bool(val));

	val = eu_value_get_cstr(var_val, "null");
	assert(eu_value_type(val) == EU_JSON_NULL);

	val = eu_value_get_cstr(var_val, "array");
	assert(eu_value_type(val) == EU_JSON_ARRAY);
	assert(eu_value_to_array(val)->len == 2);
}

static void test_parse_variant(void)
{
	TEST_PARSE("  {  \"str\\\\\\\"\":  \"hello, world!\","
		   "  \"obj\"  :  {  \"num\"  :  4.2  },"
		   "  \"bool\"  :  true  ,"
		   "  \"null\"  :  null  ,"
		   "  \"array\"  :  [  \"element\"  ,  [  ]  ]  }  ",
		   struct eu_variant,
		   eu_variant_value,
		   check_variant(&result),
		   eu_variant_fini(&result));
}

static void test_parse_deep(void)
{
	int depth = 100;

	char open[] = "  [  {  \"ab\"  :";
	char mid[] = "  100  ";
	char close[] = "}  ]  ";

	size_t open_len = strlen(open);
	size_t mid_len = strlen(mid);
	size_t close_len = strlen(close);
	size_t len = (open_len + close_len) * depth + mid_len;

	char *s = malloc(len);
	char *p = s;

	int i;
	size_t j;
	struct eu_parse *parse;
	struct eu_variant var;
	struct eu_value val;

	/* construct the test JSON string */
	for (i = 0; i < depth; i++, p += open_len)
		memcpy(p, open, open_len);

	memcpy(p, mid, mid_len);
	p += mid_len;

	for (i = 0; i < depth; i++, p += close_len)
		memcpy(p, close, close_len);

	/* parse it in 1-byte chunks */
	parse = eu_parse_create(eu_variant_value(&var));

	for (j = 0; j < len; j++) {
		char c = s[j];
		assert(eu_parse(parse, &c, 1));
	}

	assert(eu_parse_finish(parse));
	eu_parse_destroy(parse);

	/* check the result */
	val = eu_variant_value(&var);
	for (i = 0; i < depth; i++) {
		struct eu_variant_array *a
			= (struct eu_variant_array *)eu_value_to_array(val);
		assert(a->len == 1);
		val = eu_variant_value(&a->a[0]);
		assert(eu_value_ok(val = eu_value_get_cstr(val, "ab")));
	}

	assert(eu_value_to_double(val).ok);
	assert(eu_value_to_double(val).value == 100);
	eu_variant_fini(&var);
	free(s);
}

static void parse_variant(const char *json, struct eu_variant *var)
{
	struct eu_parse *parse;

	parse = eu_parse_create(eu_variant_value(var));
	assert(eu_parse(parse, json, strlen(json)));
	assert(eu_parse_finish(parse));
	eu_parse_destroy(parse);
}

static void test_path(void)
{
	struct eu_variant var;
	struct eu_value val;

	parse_variant("{\"a\":{\"b\":null,\"c\":true}}", &var);
	val = eu_variant_value(&var);
	assert(!eu_value_ok(eu_get_path(val, eu_cstr("/nosuch"))));
	val = eu_get_path(val, eu_cstr("/a/c"));
	assert(eu_value_type(val) == EU_JSON_BOOL);
	assert(*eu_value_to_bool(val));
	eu_variant_fini(&var);

	parse_variant("[10, 20, 30]", &var);
	val = eu_variant_value(&var);
	val = eu_get_path(val, eu_cstr("/1"));
	assert(eu_value_type(val) == EU_JSON_NUMBER);
	assert(eu_value_to_double(val).ok);
	assert(eu_value_to_double(val).value == 20);
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

static void test_gen_string(void)
{
	struct eu_string str;

	assert(eu_string_init(&str, eu_cstr("hello")));
	test_gen(eu_string_value(&str), eu_cstr("\"hello\""));

	assert(eu_string_assign(&str, eu_cstr("\\\"\b\t\n\f\r\036")));
	test_gen(eu_string_value(&str),
		 eu_cstr("\"\\\\\\\"\\b\\t\\n\\f\\r\\u001e\""));

	eu_string_fini(&str);
}

static void test_gen_null(void)
{
	test_gen(eu_null_value(), eu_cstr("null"));
}

static void test_gen_bool(void)
{
	eu_bool_t t = 1, f = 0;
	test_gen(eu_bool_value(&t), eu_cstr("true"));
	test_gen(eu_bool_value(&f), eu_cstr("false"));
}

static void test_gen_number(void)
{
	eu_number_t num = 100;
	test_gen(eu_number_value(&num), eu_cstr("100"));

	num = 0;
	test_gen(eu_number_value(&num), eu_cstr("0"));

	num = -12345678910;
	test_gen(eu_number_value(&num), eu_cstr("-12345678910"));

	num = 1.23;
	test_gen(eu_number_value(&num), eu_cstr("1.23"));

	num = -1.234567891234567e-10;
	test_gen(eu_number_value(&num), eu_cstr("-1.234567891234567e-10"));
}

static void test_gen_variant(void)
{
	struct eu_variant var;

	eu_variant_init(&var);

	eu_variant_assign_null(&var);
	test_gen(eu_variant_value(&var), eu_cstr("null"));

	eu_variant_assign_bool(&var, 1);
	test_gen(eu_variant_value(&var), eu_cstr("true"));

	eu_variant_assign_number(&var, 100);
	test_gen(eu_variant_value(&var), eu_cstr("100"));

	eu_variant_assign_number(&var, 123.456);
	test_gen(eu_variant_value(&var), eu_cstr("123.456"));

	eu_variant_assign_integer(&var, 100);
	test_gen(eu_variant_value(&var), eu_cstr("100"));

	assert(eu_variant_assign_string(&var, eu_cstr("hello")));
	test_gen(eu_variant_value(&var), eu_cstr("\"hello\""));

	eu_variant_fini(&var);
}

static void test_gen_object(void)
{
	struct eu_object obj;
	struct eu_object *subobj;
	struct eu_variant *var;

	eu_object_init(&obj);
	test_gen(eu_object_value(&obj), eu_cstr("{}"));

	assert(var = eu_object_get(&obj, eu_cstr("foo")));
	assert(eu_variant_assign_string(var, eu_cstr("bar")));
	test_gen(eu_object_value(&obj), eu_cstr("{\"foo\":\"bar\"}"));

	assert(var = eu_object_get(&obj, eu_cstr("bar")));
	eu_variant_assign_number(var, 100);
	test_gen(eu_object_value(&obj),
		 eu_cstr("{\"foo\":\"bar\",\"bar\":100}"));

	assert(var = eu_object_get(&obj, eu_cstr("foo")));
	eu_variant_assign_bool(var, 1);
	test_gen(eu_object_value(&obj), eu_cstr("{\"foo\":true,\"bar\":100}"));

	assert(var = eu_object_get(&obj, eu_cstr("\\\"\b\t\n\f\r\036")));
	subobj = eu_variant_assign_object(var);
	assert(var = eu_object_get(subobj, eu_cstr("null")));
	eu_variant_assign_null(var);
	test_gen(eu_object_value(&obj),
		 eu_cstr("{\"foo\":true,\"bar\":100,"
		       "\"\\\\\\\"\\b\\t\\n\\f\\r\\u001e\":{\"null\":null}}"));

	eu_object_fini(&obj);
}

static void test_gen_array(void)
{
	struct eu_variant_array a;
	struct eu_variant *var;
	int i;

	eu_variant_array_init(&a);
	test_gen(eu_variant_array_value(&a), eu_cstr("[]"));

	for (i = 0; i < 10; i++) {
		assert(var = eu_variant_array_push(&a));
		eu_variant_assign_number(var, i);
	}

	test_gen(eu_variant_array_value(&a), eu_cstr("[0,1,2,3,4,5,6,7,8,9]"));
	eu_variant_array_fini(&a);
}

int main(void)
{
	test_parse_string();
	test_parse_number();
	test_parse_number_truncated();
	test_parse_bool();
	test_parse_variant();
	test_parse_deep();

	test_path();
	test_size();

	test_gen_string();
	test_gen_null();
	test_gen_bool();
	test_gen_number();
	test_gen_variant();
	test_gen_object();
	test_gen_array();

	return 0;
}

