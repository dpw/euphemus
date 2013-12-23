#include "euphemus.h"

#include <string.h>
#include <assert.h>

#include "test_schema.h"
#include "test_parse.h"

static void check_foo(struct foo *foo)
{
	assert(foo->str.len == 1);
	assert(*foo->str.chars == 'x');
	assert(eu_variant_type(&foo->any) == EU_JSON_NULL);

	assert(foo->bar);
	assert(foo->bar->num == 42);
	assert(foo->bar->bool);
}

static void test_struct_ptr(void)
{
	TEST_PARSE("{\"str\":\"x\",\"any\":null,\"bar\":{\"num\":42,\"bool\":true}}",
		   struct foo *,
		   eu_parse_init_struct_foo,
		   check_foo(result),
		   foo_destroy(result));
}

static void test_inline_struct(void)
{
	TEST_PARSE("{\"str\":\"x\",\"any\":null,\"bar\":{\"num\":42,\"bool\":true}}",
		   struct foo,
		   eu_parse_init_inline_struct_foo,
		   check_foo(&result),
		   foo_fini(&result));
}

static void test_nested(void)
{
	TEST_PARSE("{\"bar\":{\"bar\":{\"bar\":{\"bool\":true}}}}",
		   struct foo *,
		   eu_parse_init_struct_foo,
		   assert(result->bar->bar->bar->bool),
		   foo_destroy(result));
}

static void check_extras(struct foo *foo)
{
	struct eu_variant *var = eu_variant_members_get(&foo->extras,
							eu_cstr("quux"));
	assert(var);
	assert(var->u.string.len == 1);
	assert(!memcmp(var->u.string.chars, "x", 1));
}

static void test_extras(void)
{
	TEST_PARSE("{\"quux\":\"x\"}",
		   struct foo,
		   eu_parse_init_inline_struct_foo,
		   check_extras(&result),
		   foo_fini(&result));
}

static void test_resolve(void)
{
	struct foo foo;
	struct eu_parse ep;
	const char *json = "{\"bar\":{\"bar\":{\"bar\":{\"bool\":true}}}}";
	struct eu_value val;

	eu_parse_init_inline_struct_foo(&ep, &foo);
	assert(eu_parse(&ep, json, strlen(json)));
	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);

	val = eu_value(&foo, &inline_struct_foo_metadata.base);
	assert(eu_resolve_path(&val, eu_cstr("/bar/bar/bar/bool")));
	assert(eu_value_type(val) == EU_JSON_BOOL);
	assert(*(eu_bool_t *)val.value);

	foo_fini(&foo);
}

int main(void)
{
	test_struct_ptr();
	test_inline_struct();
	test_nested();
	test_extras();
	test_resolve();
	return 0;
}
