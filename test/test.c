#include <euphemus.h>

#include <string.h>
#include <assert.h>

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
		   double,
		   eu_number_value,
		   assert(result == (double)123456789.0123456789e0),);
	TEST_PARSE("  -0.0123456789e-10  ",
		   double,
		   eu_number_value,
		   assert(result == (double)-0.0123456789e-10),);
	TEST_PARSE("  0  ",
		   double,
		   eu_number_value,
		   assert(result == 0),);
	TEST_PARSE("  123456789  ",
		   double,
		   eu_number_value,
		   assert(result == (double)123456789),);
	TEST_PARSE("  -123456789  ",
		   double,
		   eu_number_value,
		   assert(result == (double)-123456789),);
	TEST_PARSE("  1000000000000000000000000  ",
		   double,
		   eu_number_value,
		   assert(result == (double)1000000000000000000000000.0),);
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
	assert(*eu_value_to_number(val) = 42);

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
		   "  \"obj\"  :  {  \"num\"  :  42  },"
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

	assert(*eu_value_to_number(val) == 100);
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

static void test_gen(struct eu_value value, const char *expected_cstr)
{
	struct eu_string_ref expected = eu_cstr(expected_cstr);
	struct eu_generate *eg;
	char *buf = malloc(expected.len + 100);
	char *buf2 = malloc(expected.len + 100);
	size_t i, len, len2;

	/* Test generation in one go. */
	eg = eu_generate_create(value);
	len = eu_generate(eg, buf, expected.len + 1);
	assert(len <= expected.len);
	assert(!eu_generate(eg, buf + len, 1));
	assert(eu_generate_ok(eg));
	eu_generate_destroy(eg);
	assert(eu_string_ref_equal(eu_string_ref(buf, len), expected));

	/* Test generation broken into two chunks */
	for (i = 0;; i++) {
		eg = eu_generate_create(value);
		len = eu_generate(eg, buf, i);
		assert(len <= i);
		len2 = eu_generate(eg, buf2, expected.len + 1);
		assert(len + len2 <= expected.len);
		assert(eu_generate_ok(eg));
		eu_generate_destroy(eg);

		memcpy(buf + i, buf2, len2);
		assert(eu_string_ref_equal(eu_string_ref(buf, len + len2),
					   expected));

		if (len2 == 0)
			break;
	}

	/* Test byte-at-a-time generation */
	eg = eu_generate_create(value);
	len = 0;

	while (eu_generate(eg, buf2, 1)) {
		assert(len < expected.len);
		buf[len++] = *buf2;
	}

	assert(eu_generate_ok(eg));
	eu_generate_destroy(eg);
	assert(eu_string_ref_equal(eu_string_ref(buf, len), expected));

	/* Test that resources are released after an unfinished generation. */
	eg = eu_generate_create(value);
	eu_generate_destroy(eg);

	for (i = 0;; i++) {
		eg = eu_generate_create(value);
		len = eu_generate(eg, buf, i);
		eu_generate_destroy(eg);

		if (len < i)
			break;
	}

	free(buf);
	free(buf2);
}

static void test_gen_string(void)
{
	struct eu_string str;

	eu_string_init(&str);
	assert(eu_string_assign(&str, eu_cstr("hello")));
	test_gen(eu_string_value(&str), "\"hello\"");
	eu_string_fini(&str);
}

static void test_gen_null(void)
{
	test_gen(eu_null_value(), "null");
}

static void test_gen_bool(void)
{
	eu_bool_t t = 1, f = 0;
	test_gen(eu_bool_value(&t), "true");
	test_gen(eu_bool_value(&f), "false");
}

static void test_gen_number(void)
{
	double num = 100;
	test_gen(eu_number_value(&num), "100");

	num = 0;
	test_gen(eu_number_value(&num), "0");

	num = -12345678910;
	test_gen(eu_number_value(&num), "-12345678910");

	num = 1.23;
	test_gen(eu_number_value(&num), "1.23");

	num = -1.234567891234567e-10;
	test_gen(eu_number_value(&num), "-1.234567891234567e-10");
}

static void test_gen_object(void)
{
	struct eu_object obj;

	eu_object_init(&obj);
	assert(eu_object_get(&obj, eu_cstr("foo")));
	eu_object_fini(&obj);
}

static void test_gen_variant(void)
{
	struct eu_variant var;

	eu_variant_init_null(&var);
	test_gen(eu_variant_value(&var), "null");
	eu_variant_fini(&var);

	eu_variant_init_bool(&var, 1);
	test_gen(eu_variant_value(&var), "true");
	eu_variant_fini(&var);

	eu_variant_init_number(&var, 100);
	test_gen(eu_variant_value(&var), "100");
	eu_variant_fini(&var);

	eu_variant_init_string(&var);
	eu_string_assign(&var.u.string, eu_cstr("hello"));
	test_gen(eu_variant_value(&var), "\"hello\"");
	eu_variant_fini(&var);
}

int main(void)
{
	test_parse_string();
	test_parse_number();
	test_parse_bool();
	test_parse_variant();
	test_parse_deep();

	test_path();
	test_size();

	test_gen_string();
	test_gen_null();
	test_gen_bool();
	test_gen_number();
	test_gen_object();
	test_gen_variant();

	return 0;
}

