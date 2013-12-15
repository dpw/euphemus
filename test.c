#include "euphemus.h"

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



static void test_string(void)
{
	TEST_PARSE("  \"hello, world!\"  ",
		   struct eu_string,
		   eu_parse_init_string,
		   (assert(result.len ==13),
		    assert(!memcmp(result.chars, "hello, world!", 13))),
		   eu_string_fini(&result));
}

static void test_number(void)
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
}

static void test_bool(void)
{
	TEST_PARSE("  true  ", eu_bool_t, eu_parse_init_bool,
		   assert(result),);
	TEST_PARSE("  false  ", eu_bool_t, eu_parse_init_bool,
		   assert(!result),);
}


static void check_array(struct eu_string_array *a)
{
	assert(a->len == 2);

	assert(a->a[0].len == 3);
	assert(!memcmp(a->a[0].chars, "foo", 3));

	assert(a->a[1].len == 3);
	assert(!memcmp(a->a[1].chars, "bar", 3));
}

static void test_array(void)
{
	TEST_PARSE("  [  \"foo\"  ,  \"bar\"  ]  ",
		   struct eu_string_array,
		   eu_parse_init_string_array,
		   check_array(&result),
		   eu_string_array_fini(&result));
}


static void check_variant(struct eu_variant *var)
{
	struct eu_variant *str, *obj, *num, *bool, *null, *array;

	assert(eu_variant_type(var) == EU_JSON_OBJECT);
	assert(str = eu_variant_get(var, "str"));
	assert(obj = eu_variant_get(var, "obj"));
	assert(bool = eu_variant_get(var, "bool"));
	assert(null = eu_variant_get(var, "null"));
	assert(array = eu_variant_get(var, "array"));

	assert(eu_variant_type(str) == EU_JSON_STRING);
	assert(str->u.string.len = 13);
	assert(!memcmp(str->u.string.chars, "hello, world!", 13));

	assert(eu_variant_type(obj) == EU_JSON_OBJECT);
	assert(num = eu_variant_get(obj, "num"));

	assert(eu_variant_type(num) == EU_JSON_NUMBER);
	assert(num->u.number = 42);

	assert(eu_variant_type(bool) == EU_JSON_BOOL);
	assert(bool->u.bool);

	assert(eu_variant_type(null) == EU_JSON_NULL);

	assert(eu_variant_type(array) == EU_JSON_ARRAY);
	assert(array->u.array.len == 2);
	assert(eu_variant_type(&array->u.array.a[0]) == EU_JSON_STRING);
	assert(eu_variant_type(&array->u.array.a[1]) == EU_JSON_ARRAY);
}

static void test_variant(void)
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

int main(void)
{
	test_string();
	test_number();
	test_bool();
	test_array();
	test_variant();
	return 0;
}
