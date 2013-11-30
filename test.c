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

static struct eu_struct_metadata inline_struct_foo_metadata
	= EU_INLINE_STRUCT_METADATA_INITIALIZER(struct foo, foo_members);

static struct eu_metadata *const inline_foo_start
	= &inline_struct_foo_metadata.base;

void foo_destroy(struct foo *foo);

void foo_fini(struct foo *foo)
{
	if (foo->bar)
		foo_destroy(foo->bar);

	eu_string_fini(&foo->baz);
	eu_open_struct_fini(&foo->open);
}

void foo_destroy(struct foo *foo)
{
	foo_fini(foo);
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

	for (i = 0; i < len; i++) {
		eu_parse_init(&ep, start, result);
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

static void test_string(void)
{
	struct eu_string string;

	test_parse("  \"hello, world!\"  ", eu_string_start,
		   &string, validate_string);
}

static void validate_variant(void *v_variant)
{
	struct eu_variant *var = v_variant;
	struct eu_variant *foo, *bar, *baz;

	assert(eu_variant_type(var) == EU_JSON_OBJECT);
	assert(foo = eu_variant_get(var, "foo"));
	assert(bar = eu_variant_get(var, "bar"));

	assert(eu_variant_type(foo) == EU_JSON_STRING);
	assert(foo->u.string.len = 13);
	assert(!memcmp(foo->u.string.string, "hello, world!", 13));

	assert(eu_variant_type(bar) == EU_JSON_OBJECT);
	assert(baz = eu_variant_get(bar, "baz"));

	assert(eu_variant_type(baz) == EU_JSON_STRING);
	assert(baz->u.string.len = 4);
	assert(!memcmp(baz->u.string.string, "bye!", 4));

	eu_variant_fini(var);
}

static void test_variant(void)
{
	struct eu_variant var;

	test_parse("  {  \"foo\":  \"hello, world!\","
		   "  \"bar\":  {  \"baz\"  :  \"bye!\"  }  }  ",
		   eu_variant_start, &var, validate_variant);
}

static void validate_foo(struct foo *foo)
{
	assert(foo->bar);
	assert(!foo->bar->bar);
	assert(!foo->bar->baz.len);
	assert(foo->baz.len == 1);
	assert(*foo->baz.string == 'x');
}

static void validate_struct_foo(void *v_foo_ptr)
{
	struct foo *foo = *(struct foo **)v_foo_ptr;

	assert(foo);
	validate_foo(foo);
	foo_destroy(foo);
}

static void test_struct(void)
{
	struct foo *foo;

	test_parse("  {  \"bar\"  :  {  }  , \"baz\"  :  \"x\"  }  ", foo_start,
		   &foo, validate_struct_foo);
}

static void validate_inline_struct_foo(void *v_foo)
{
	struct foo *foo = v_foo;

	validate_foo(foo);
	foo_fini(foo);
}

static void test_inline_struct(void)
{
	struct foo foo;

	test_parse("  {  \"bar\"  :  {  }  , \"baz\"  :  \"x\"  }  ",
		   inline_foo_start, &foo, validate_inline_struct_foo);
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
	test_inline_struct();
	test_variant();
	test_extras();
	return 0;
}
