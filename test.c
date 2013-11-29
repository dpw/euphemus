#include "euphemus.h"

#include <string.h>
#include <assert.h>

struct foo {
	struct foo *bar;
	struct eu_string baz;
	struct eu_open_struct open;
};

static struct eu_struct_metadata struct_foo_metadata;

static struct eu_struct_member foo_members[] = {
	{
		offsetof(struct foo, bar),
		3,
		"bar",
		&struct_foo_metadata.base
	},
	{
		offsetof(struct foo, baz),
		3,
		"baz",
		&eu_string_metadata
	}
};

static struct eu_struct_metadata struct_foo_metadata
	= EU_STRUCT_METADATA_INITIALIZER(struct foo, foo_members);

static struct eu_metadata *const foo_start = &struct_foo_metadata.base;

void foo_destroy(struct foo *foo)
{
	if (foo->bar)
		foo_destroy(foo->bar);

	eu_string_fini(&foo->baz);
	eu_open_struct_fini(&foo->open);
	free(foo);
}

static void test_parse(const char *json, struct eu_metadata *start,
		       void *result, void (*validate)(void *result))
{
	struct eu_parse ep;
	size_t len = strlen(json);
	size_t i;

	/* Test parsing in one go */
	eu_parse_init(&ep, start, result);
	assert(eu_parse(&ep, json, len));
	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);
	validate(result);

	/* Test parsing broken at each position within the json */
	for (i = 0; i < len; i++) {
		eu_parse_init(&ep, start, result);
		assert(eu_parse(&ep, json, i));
		assert(eu_parse(&ep, json + i, len - i));
		assert(eu_parse_finish(&ep));
		eu_parse_fini(&ep);
		validate(result);
	}

	/* Test parsing with the json broken into individual bytes */
	eu_parse_init(&ep, start, result);
	for (i = 0; i < len; i++)
		assert(eu_parse(&ep, json + i, 1));

	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);
	validate(result);

	/* Test that resources are released after an unfinished parse. */
	eu_parse_init(&ep, start, result);
	eu_parse_fini(&ep);

	eu_parse_init(&ep, start, result);
	assert(eu_parse(&ep, json, len / 2));
	eu_parse_fini(&ep);
}

static void validate_string(void *v_string)
{
	struct eu_string *string = v_string;

	assert(string->len == 13);
	assert(!memcmp(string->string, "hello, world!", 13));
	eu_string_fini(string);
}

static void test_string(void)
{
	struct eu_string string;

	test_parse("  \"hello, world!\"  ", eu_string_start,
		   &string, validate_string);
}

static void validate_variant(void *v_variant)
{
	struct eu_variant *var = v_variant;

	assert(var->u.string.len == 13);
	assert(!memcmp(var->u.string.string, "hello, world!", 13));
	eu_variant_fini(var);
}

static void test_variant(void)
{
	struct eu_variant var;

	test_parse("  \"hello, world!\"  ", eu_variant_start,
		   &var, validate_variant);
}

static void validate_foo(void *v_foo)
{
	struct foo *foo = *(struct foo **)v_foo;

	assert(foo);
	assert(foo->bar);
	assert(!foo->bar->bar);
	assert(!foo->bar->baz.len);
	assert(foo->baz.len == 1);
	assert(*foo->baz.string == 'x');
	foo_destroy(foo);
}

static void test_struct(void)
{
	struct foo *foo;

	test_parse("  {  \"bar\"  :  {  }  , \"baz\"  :  \"x\"  }  ", foo_start,
		   &foo, validate_foo);
}

static void validate_extras(void *v_foo)
{
	struct foo *foo = *(struct foo **)v_foo;
	struct eu_variant *var = eu_open_struct_get(&foo->open, "quux");

	assert(var);
	assert(var->u.string.len == 1);
	assert(!memcmp(var->u.string.string, "x", 1));
	foo_destroy(foo);
}

static void test_extras(void)
{
	struct foo *foo;

	test_parse("  {  \"quux\"  :  \"x\"  }  ", foo_start,
		   &foo, validate_extras);
}

int main(void)
{
	test_string();
	test_struct();
	test_variant();
	test_extras();
	return 0;
}
