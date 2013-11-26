#include "euphemus.h"

#include <stddef.h>
#include <string.h>
#include <assert.h>

struct foo {
	struct foo *bar;
	struct eu_string baz;
};

static struct struct_metadata struct_foo_metadata;

static struct struct_member foo_members[] = {
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

static struct struct_metadata struct_foo_metadata = {
	{
		{
			NULL,
			eu_parse_metadata_resume,
			eu_parse_cont_noop_dispose
		},
		struct_parse,
		struct_dispose
	},
	sizeof(struct foo),
	2,
	foo_members
};

static struct eu_metadata *const foo_start = &struct_foo_metadata.base;

void foo_destroy(struct foo *foo)
{
	if (foo->bar)
		foo_destroy(foo->bar);

	eu_string_fini(&foo->baz);
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

void test_string(void)
{
	struct eu_string string;

	test_parse("  \"hello, world!\"  ", eu_string_start,
		   &string, validate_string);
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

int main(void)
{
	test_string();
	test_struct();
	return 0;
}
