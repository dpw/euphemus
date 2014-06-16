#include <euphemus.h>
#include "euphemus_int.h"

void eu_noop_fini(const struct eu_metadata *metadata, void *value)
{
	(void)metadata;
	(void)value;
}

static enum eu_result fail_parse(const struct eu_metadata *metadata,
				 struct eu_parse *ep, void *result)
{
	(void)metadata;
	(void)ep;
	(void)result;
	return EU_ERROR;
}

enum eu_result eu_generate_fail(const struct eu_metadata *metadata,
				struct eu_generate *eg, void *value)
{
	(void)metadata;
	(void)eg;
	(void)value;
	return EU_ERROR;
}

static void fail_fini(const struct eu_metadata *metadata, void *value)
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

int eu_object_iter_init_fail(struct eu_value val, struct eu_object_iter *iter)
{
	(void)val;
	(void)iter;
	return 0;
}

size_t eu_object_size_fail(struct eu_value val)
{
	(void)val;
	return 0;
}

struct eu_maybe_double eu_to_double_fail(struct eu_value val)
{
	struct eu_maybe_double res = { 0, 0.0 };
	(void)val;
	return res;
}

struct eu_maybe_integer eu_to_integer_fail(struct eu_value val)
{
	struct eu_maybe_integer res = { 0, 0 };
	(void)val;
	return res;
}

static struct eu_metadata fail_metadata = {
	EU_JSON_INVALID,
	0,
	fail_parse,
	eu_generate_fail,
	fail_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size_fail,
	eu_to_double_fail,
	eu_to_integer_fail,
};

const struct eu_metadata *eu_introduce(const struct eu_type_descriptor *d)
{
	const struct eu_metadata *res = eu_introduce_aux(d, NULL);
	return res != NULL ? res : &fail_metadata;
}

const struct eu_metadata *eu_introduce_aux(const struct eu_type_descriptor *d,
					   struct eu_introduce_chain *chain)
{
	struct eu_introduce_chain *c;
	const struct eu_metadata *md = *d->metadata;

	if (md)
		return md;

	/* Check whether we are already processing this descriptor
	   somewhere up the stack. */
	for (c = chain; c != NULL; c = c->next)
		if (d == c->descriptor)
			return c->metadata;

	switch (d->kind) {
	case EU_TDESC_STRUCT_V1:
		return eu_introduce_struct(d, chain);

	case EU_TDESC_STRUCT_PTR_V1:
		return eu_introduce_struct_ptr(d, chain);

	case EU_TDESC_ARRAY_V1:
		return eu_introduce_array(d, chain);

	default:
		return NULL;
	}
}

#define DEFINE_SHIM_DESCRIPTOR(name, md)                              \
static const struct eu_metadata *name##_metadata_ptr = md;            \
const struct eu_type_descriptor eu_##name##_descriptor = {            \
	&name##_metadata_ptr,                                         \
	EU_TDESC_SHIM                                                 \
};

DEFINE_SHIM_DESCRIPTOR(string, &eu_string_metadata)
DEFINE_SHIM_DESCRIPTOR(number, (const struct eu_metadata *)&eu_number_metadata)
DEFINE_SHIM_DESCRIPTOR(integer,
		       (const struct eu_metadata *)&eu_integer_metadata)
DEFINE_SHIM_DESCRIPTOR(bool, &eu_bool_metadata)
DEFINE_SHIM_DESCRIPTOR(null, &eu_null_metadata)
DEFINE_SHIM_DESCRIPTOR(variant, &eu_variant_metadata)

struct eu_value eu_value_get(struct eu_value val, struct eu_string_ref name)
{
	return val.metadata->get(val, name);
}

enum eu_json_type eu_value_type(struct eu_value val)
{
	enum eu_json_type t;

	if (!val.metadata)
		return EU_JSON_INVALID;

	t = val.metadata->json_type;
	if (t != EU_JSON_VARIANT)
		return t;
	else
		return ((struct eu_variant *)val.value)->metadata->json_type;
}

void *eu_value_extract(struct eu_value val, enum eu_json_type type)
{
	enum eu_json_type t = val.metadata->json_type;

	if (t == type)
		return val.value;

	if (t == EU_JSON_VARIANT) {
		struct eu_variant *var = val.value;
		if (var->metadata->json_type == type)
			return &var->u;
	}

	abort();
}

struct eu_maybe_double eu_value_to_double(struct eu_value val)
{
	return val.metadata->to_double(val);
}

struct eu_maybe_integer eu_value_to_integer(struct eu_value val)
{
	return val.metadata->to_integer(val);
}


