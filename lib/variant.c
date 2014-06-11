#include <limits.h>

#include <euphemus.h>
#include "euphemus_int.h"

static enum eu_result invalid(const void *misc, struct eu_parse *ep,
			      struct eu_variant *result)
{
	(void)misc;
	(void)ep;
	(void)result;
	return EU_ERROR;
}

static enum eu_result whitespace(const void *variant_metadata,
				 struct eu_parse *ep, struct eu_variant *result);

struct char_type_slot {
	enum eu_result (*func)(const void *misc, struct eu_parse *ep,
			       struct eu_variant *result);
	const void *misc;
};


enum char_type {
	CHAR_TYPE_INVALID,
	CHAR_TYPE_WS,
	CHAR_TYPE_DQUOTES,
	CHAR_TYPE_BRACE,
	CHAR_TYPE_BRACKET,
	CHAR_TYPE_NUMBER,
	CHAR_TYPE_T,
	CHAR_TYPE_F,
	CHAR_TYPE_N,
	CHAR_TYPE_MAX
};

/* Mapping from characters to character types. */
static const unsigned char char_types[UCHAR_MAX] CACHE_ALIGN = {
	[' '] = CHAR_TYPE_WS,
	['\t'] = CHAR_TYPE_WS,
	['\n'] = CHAR_TYPE_WS,
	['\r'] = CHAR_TYPE_WS,

	['\"'] = CHAR_TYPE_DQUOTES,
	['{'] = CHAR_TYPE_BRACE,
	['['] = CHAR_TYPE_BRACKET,

	['0'] = CHAR_TYPE_NUMBER,
	['1'] = CHAR_TYPE_NUMBER,
	['2'] = CHAR_TYPE_NUMBER,
	['3'] = CHAR_TYPE_NUMBER,
	['4'] = CHAR_TYPE_NUMBER,
	['5'] = CHAR_TYPE_NUMBER,
	['6'] = CHAR_TYPE_NUMBER,
	['7'] = CHAR_TYPE_NUMBER,
	['8'] = CHAR_TYPE_NUMBER,
	['9'] = CHAR_TYPE_NUMBER,
	['-'] = CHAR_TYPE_NUMBER,

	['t'] = CHAR_TYPE_T,
	['f'] = CHAR_TYPE_F,

	['n'] = CHAR_TYPE_N,
};

static const struct char_type_slot char_type_slots[CHAR_TYPE_MAX] = {
	[CHAR_TYPE_INVALID] = { invalid, NULL },
	[CHAR_TYPE_WS] = { whitespace, &eu_variant_metadata },
	[CHAR_TYPE_DQUOTES] = { eu_variant_string, &eu_string_metadata },
	[CHAR_TYPE_BRACE] = { eu_variant_object, NULL },
	[CHAR_TYPE_BRACKET] = { eu_variant_array, NULL },
	[CHAR_TYPE_NUMBER] = { eu_variant_number, &eu_number_metadata },
	[CHAR_TYPE_T] = { eu_variant_bool, &eu_bool_true },
	[CHAR_TYPE_F] = { eu_variant_bool, &eu_bool_false },
	[CHAR_TYPE_N] = { eu_variant_n, &eu_null_metadata },
};

static enum eu_result variant_parse(const struct eu_metadata *metadata,
				    struct eu_parse *ep, void *result)
{
	unsigned char ct = char_types[(unsigned char)*ep->input];
	struct char_type_slot slot = char_type_slots[ct];
	(void)metadata;
	return slot.func(slot.misc, ep, result);
}

static enum eu_result whitespace(const void *variant_metadata,
				 struct eu_parse *ep, struct eu_variant *result)
{
	enum eu_result res = eu_consume_whitespace(variant_metadata, ep, result);
	if (res == EU_OK)
		return variant_parse(variant_metadata, ep, result);
	else
		return res;
}

static enum eu_result variant_generate(const struct eu_metadata *metadata,
				       struct eu_generate *eg, void *value)
{
	struct eu_variant *var = value;
	(void)metadata;
	return var->metadata->generate(var->metadata, eg, &var->u);
}

void eu_variant_fini(struct eu_variant *variant)
{
	if (variant->metadata)
		variant->metadata->fini(variant->metadata, &variant->u);
}

void eu_variant_reset(struct eu_variant *variant)
{
	if (variant->metadata) {
		variant->metadata->fini(variant->metadata, &variant->u);
		variant->metadata = NULL;
	}
}

static void variant_fini(const struct eu_metadata *metadata, void *value)
{
	struct eu_variant *var = value;
	(void)metadata;

	if (var->metadata) {
		var->metadata->fini(var->metadata, &var->u);
		var->metadata = NULL;
	}
}

static struct eu_value eu_variant_peek(struct eu_variant *variant)
{
	return eu_value(&variant->u, variant->metadata);
}

static struct eu_value variant_get(struct eu_value val,
				   struct eu_string_ref name)
{
	struct eu_variant *var = val.value;
	return eu_value_get(eu_variant_peek(var), name);
}

static int variant_object_iter_init(struct eu_value val,
				    struct eu_object_iter *iter)
{
	struct eu_variant *var = val.value;
	return eu_object_iter_init(iter, eu_variant_peek(var));
}

static size_t variant_object_size(struct eu_value val)
{
	struct eu_variant *var = val.value;
	return eu_object_size(eu_variant_peek(var));
}

static struct eu_maybe_double variant_to_double(struct eu_value val)
{
	struct eu_variant *var = val.value;
	return eu_value_to_double(eu_variant_peek(var));
}

const struct eu_metadata eu_variant_metadata = {
	EU_JSON_VARIANT,
	sizeof(struct eu_variant),
	variant_parse,
	variant_generate,
	variant_fini,
	variant_get,
	variant_object_iter_init,
	variant_object_size,
	variant_to_double,
};
