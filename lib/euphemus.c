#include <euphemus.h>
#include "euphemus_int.h"

static enum eu_parse_result fail_parse(struct eu_metadata *metadata,
				       struct eu_parse *ep,
				       void *result)
{
	(void)metadata;
	(void)ep;
	(void)result;
	return EU_PARSE_ERROR;
}

static void fail_fini(struct eu_metadata *metadata, void *value)
{
	(void)metadata;
	(void)value;
}

struct eu_value eu_get_fail(struct eu_value val, struct eu_string_ref name)
{
	(void)val;
	(void)name;
	return eu_value_none;
}

void eu_object_iter_init_fail(struct eu_value val, struct eu_object_iter *iter)
{
	(void)val;
	(void)iter;
	abort();
}

static struct eu_metadata fail_metadata = {
	EU_JSON_INVALID,
	0,
	fail_parse,
	fail_fini,
	eu_get_fail,
	eu_object_iter_init_fail
};

struct eu_metadata *eu_introduce(const struct eu_type_descriptor *d)
{
	struct eu_metadata *res = eu_introduce_aux(d, NULL);
	return res != NULL ? res : &fail_metadata;
}

struct eu_metadata *eu_introduce_aux(const struct eu_type_descriptor *d,
				     struct eu_introduce_chain *chain)
{
	struct eu_introduce_chain *c;
	struct eu_metadata *md = *d->metadata;

	if (md)
		return md;

	/* Check whether we are already processing this descriptor
	   somewhere up the stack. */
	for (c = chain; c != NULL; c = c->next)
		if (d == c->descriptor)
			return c->metadata;

	switch (d->kind) {
	case EU_TDESC_STRUCT:
		return eu_introduce_struct(d, chain);

	case EU_TDESC_STRUCT_PTR:
		return eu_introduce_struct_ptr(d, chain);

	default:
		return NULL;
	}
}

#define DEFINE_DIRECT_DESCRIPTOR(name)                                \
static struct eu_metadata *name##_metadata_ptr                        \
	= &eu_##name##_metadata;                                      \
const struct eu_type_descriptor eu_##name##_descriptor = {            \
	&name##_metadata_ptr,                                         \
	EU_TDESC_DIRECT                                               \
};

DEFINE_DIRECT_DESCRIPTOR(string)
DEFINE_DIRECT_DESCRIPTOR(number)
DEFINE_DIRECT_DESCRIPTOR(bool)
DEFINE_DIRECT_DESCRIPTOR(null)
DEFINE_DIRECT_DESCRIPTOR(variant)
