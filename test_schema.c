#include "euphemus.h"

struct bar {
	double num;
	struct eu_variant_members extras;
};

static struct eu_struct_member struct_bar_members[1] = {
	{
		offsetof(struct bar, num),
		3,
		"num",
		&eu_number_metadata
	},
};

struct eu_struct_metadata struct_bar_metadata
	= EU_STRUCT_METADATA_INITIALIZER(struct bar, struct_bar_members);

struct eu_struct_metadata inline_struct_bar_metadata
	= EU_INLINE_STRUCT_METADATA_INITIALIZER(struct bar, struct_bar_members);

struct foo {
	struct eu_string str;
	struct bar *bar;
	struct eu_variant_members extras;
};

static struct eu_struct_member struct_foo_members[2] = {
	{
		offsetof(struct foo, str),
		3,
		"str",
		&eu_string_metadata
	},
	{
		offsetof(struct foo, bar),
		3,
		"bar",
		&struct_bar_metadata.base
	},
};

struct eu_struct_metadata struct_foo_metadata
	= EU_STRUCT_METADATA_INITIALIZER(struct foo, struct_foo_members);

struct eu_struct_metadata inline_struct_foo_metadata
	= EU_INLINE_STRUCT_METADATA_INITIALIZER(struct foo, struct_foo_members);

