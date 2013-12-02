#include "euphemus.h"

#include <string.h>
#include <assert.h>

struct foo {
	struct foo *foo;
	struct eu_string str;
	double num;
	struct eu_open_struct open;
};

static struct eu_struct_metadata struct_foo_metadata;

static struct eu_struct_member foo_members[] = {
	{
		offsetof(struct foo, foo),
		3,
		"foo",
		&struct_foo_metadata.base
	},
	{
		offsetof(struct foo, str),
		3,
		"str",
		&eu_string_metadata
	},
	{
		offsetof(struct foo, num),
		3,
		"num",
		&eu_number_metadata
	}
};

static struct eu_struct_metadata struct_foo_metadata
	= EU_STRUCT_METADATA_INITIALIZER(struct foo, foo_members);

static struct eu_struct_metadata inline_struct_foo_metadata
	= EU_INLINE_STRUCT_METADATA_INITIALIZER(struct foo, foo_members);

void foo_destroy(struct foo *foo);

void foo_fini(struct foo *foo)
{
	if (foo->foo)
		foo_destroy(foo->foo);

	eu_string_fini(&foo->str);
	eu_open_struct_fini(&foo->open);
}

void foo_destroy(struct foo *foo)
{
	foo_fini(foo);
	free(foo);
}

void eu_parse_init_struct_foo(struct eu_parse *ep, struct foo **foo)
{
	eu_parse_init(ep, &struct_foo_metadata.base, foo);
}

void eu_parse_init_inline_struct_foo(struct eu_parse *ep, struct foo *foo)
{
	eu_parse_init(ep, &inline_struct_foo_metadata.base, foo);
}

static void test_parse(const char *json, void *result,
		       void (*parse_init)(struct eu_parse *ep, void *result),
		       void (*validate)(void *result))
{
	struct eu_parse ep;
	size_t len = strlen(json);
	size_t i;

	/* Test parsing in one go */
	parse_init(&ep, result);
	assert(eu_parse(&ep, json, len));
	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);
	validate(result);

	/* Test parsing broken at each position within the json */
	for (i = 0; i < len; i++) {
		parse_init(&ep, result);
		assert(eu_parse(&ep, json, i));
		assert(eu_parse(&ep, json + i, len - i));
		assert(eu_parse_finish(&ep));
		eu_parse_fini(&ep);
		validate(result);
	}

	/* Test parsing with the json broken into individual bytes */
	parse_init(&ep, result);
	for (i = 0; i < len; i++)
		assert(eu_parse(&ep, json + i, 1));

	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);
	validate(result);

	/* Test that resources are released after an unfinished parse. */
	parse_init(&ep, result);
	eu_parse_fini(&ep);

	for (i = 0; i < len; i++) {
		parse_init(&ep, result);
		assert(eu_parse(&ep, json, i));
		eu_parse_fini(&ep);
	}
}

static void validate_string(void *v_string)
{
	struct eu_string *string = v_string;

	assert(string->len == 13);
	assert(!memcmp(string->string, "hello, world!", 13));
	eu_string_fini(string);
}

static void parse_init_string(struct eu_parse *ep, void *str)
{
	eu_parse_init_string(ep, str);
}

static void test_string(void)
{
	struct eu_string string;

	test_parse("  \"hello, world!\"  ", &string,
		   parse_init_string, validate_string);
}


static void validate_number_a(void *number)
{
	assert(*(double *)number == 123456789.0123456789e0);
}

static void validate_number_b(void *number)
{
	assert(*(double *)number == -0.0123456789e10);
}

static void validate_number_zero(void *number)
{
	assert(*(double *)number == 0);
}

static void parse_init_number(struct eu_parse *ep, void *number)
{
	eu_parse_init_number(ep, number);
}

static void test_number(void)
{
	double number;

	test_parse("  123456789.0123456789e0  ", &number,
		   parse_init_number, validate_number_a);
	test_parse("  -0.0123456789e10  ", &number,
		   parse_init_number, validate_number_b);
	test_parse("  0  ", &number,
		   parse_init_number, validate_number_zero);
}


static void validate_true(void *bool)
{
	assert(*(eu_bool_t *)bool);
}

static void validate_false(void *bool)
{
	assert(*(eu_bool_t *)bool);
}

static void parse_init_bool(struct eu_parse *ep, void *bool)
{
	eu_parse_init_bool(ep, bool);
}

static void test_bool(void)
{
	eu_bool_t bool;

	test_parse("  true  ", &bool, parse_init_bool, validate_true);
	test_parse("  false  ", &bool, parse_init_bool, validate_false);
}


static void validate_foo(struct foo *foo)
{
	assert(foo->foo);
	assert(!foo->foo->foo);
	assert(!foo->foo->str.len);
	assert(foo->foo->num == 0);
	assert(foo->str.len == 1);
	assert(*foo->str.string == 'x');
	assert(foo->num == 42);
}

static void validate_foo_ptr(void *v_foo_ptr)
{
	struct foo *foo = *(struct foo **)v_foo_ptr;

	assert(foo);
	validate_foo(foo);
	foo_destroy(foo);
}

static void parse_init_foo_ptr(struct eu_parse *ep, void *foo_ptr)
{
	eu_parse_init_struct_foo(ep, foo_ptr);
}

static void test_struct_ptr(void)
{
	struct foo *foo;

	test_parse("  {  \"foo\"  :  {  }  , \"str\"  :  \"x\"  ,  \"num\"  :  42  }  ", &foo,
		   parse_init_foo_ptr, validate_foo_ptr);
}

static void validate_inline_foo(void *v_foo)
{
	struct foo *foo = v_foo;

	validate_foo(foo);
	foo_fini(foo);
}

static void parse_init_inline_foo(struct eu_parse *ep, void *foo)
{
	eu_parse_init_inline_struct_foo(ep, foo);
}

static void test_inline_struct(void)
{
	struct foo foo;

	test_parse("  {  \"foo\"  :  {  }  , \"str\"  :  \"x\"  ,  \"num\"  :  42  }  ", &foo,
		   parse_init_inline_foo, validate_inline_foo);
}

static void validate_extras(void *v_foo)
{
	struct foo *foo = v_foo;

	struct eu_variant *var = eu_open_struct_get(&foo->open, "quux");
	assert(var);
	assert(var->u.string.len == 1);
	assert(!memcmp(var->u.string.string, "x", 1));

	foo_fini(foo);
}

static void test_extras(void)
{
	struct foo foo;

	test_parse("  {  \"quux\"  :  \"x\"  }  ", &foo,
		   parse_init_inline_foo, validate_extras);
}

static void validate_variant(void *v_variant)
{
	struct eu_variant *var = v_variant;
	struct eu_variant *str, *obj, *num, *bool;

	assert(eu_variant_type(var) == EU_JSON_OBJECT);
	assert(str = eu_variant_get(var, "str"));
	assert(obj = eu_variant_get(var, "obj"));
	assert(bool = eu_variant_get(var, "bool"));

	assert(eu_variant_type(str) == EU_JSON_STRING);
	assert(str->u.string.len = 13);
	assert(!memcmp(str->u.string.string, "hello, world!", 13));

	assert(eu_variant_type(obj) == EU_JSON_OBJECT);
	assert(num = eu_variant_get(obj, "num"));

	assert(eu_variant_type(num) == EU_JSON_NUMBER);
	assert(num->u.number = 42);

	assert(eu_variant_type(bool) == EU_JSON_BOOL);
	assert(bool->u.bool);

	eu_variant_fini(var);
}

static void parse_init_variant(struct eu_parse *ep, void *var)
{
	eu_parse_init_variant(ep, var);
}

static void test_variant(void)
{
	struct eu_variant var;

	test_parse("  {  \"str\":  \"hello, world!\","
		   "  \"obj\"  :  {  \"num\"  :  42  },"
		   "  \"bool\"  :  true  }  ", &var,
		   parse_init_variant, validate_variant);
}

int main(void)
{
	test_string();
	test_number();
	test_struct_ptr();
	test_inline_struct();
	test_extras();
	test_variant();
	return 0;
}
