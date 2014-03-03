#include <limits.h>

#include <euphemus.h>
#include "euphemus_int.h"

static enum eu_parse_result invalid(void *misc, struct eu_parse *ep,
				    struct eu_variant *result)
{
	(void)misc;
	(void)ep;
	(void)result;
	return EU_PARSE_ERROR;
}

static enum eu_parse_result whitespace(void *variant_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result);

struct char_type_slot {
	enum eu_parse_result (*func)(void *misc, struct eu_parse *ep,
				     struct eu_variant *result);
	void *misc;
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
static unsigned char char_types[UCHAR_MAX] CACHE_ALIGN = {
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

static struct char_type_slot char_type_slots[CHAR_TYPE_MAX] = {
	[CHAR_TYPE_INVALID] = { invalid, NULL },
	[CHAR_TYPE_WS] = { whitespace, &eu_variant_metadata },
	[CHAR_TYPE_DQUOTES] = { eu_variant_string, &eu_string_metadata },
	[CHAR_TYPE_BRACE] = { eu_variant_object,
			      &eu_object_metadata.base },
	[CHAR_TYPE_BRACKET] = { eu_variant_array,
				&eu_variant_array_metadata.base },
	[CHAR_TYPE_NUMBER] = { eu_variant_number, &eu_number_metadata },
	[CHAR_TYPE_T] = { eu_variant_bool, &eu_bool_true },
	[CHAR_TYPE_F] = { eu_variant_bool, &eu_bool_false },
	[CHAR_TYPE_N] = { eu_variant_n, &eu_null_metadata },
};

static enum eu_parse_result variant_parse(struct eu_metadata *metadata,
					 struct eu_parse *ep,
					 void *result)
{
	unsigned char ct = char_types[(unsigned char)*ep->input];
	struct char_type_slot slot = char_type_slots[ct];
	(void)metadata;
	return slot.func(slot.misc, ep, result);
}

static enum eu_parse_result whitespace(void *variant_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result)
{
	enum eu_parse_result res
		= eu_consume_whitespace(variant_metadata, ep, result);
	if (res == EU_PARSE_OK)
		return variant_parse(variant_metadata, ep, result);
	else
		return res;
}

void eu_variant_fini(struct eu_variant *variant)
{
	if (variant->metadata)
		variant->metadata->fini(variant->metadata, &variant->u);
}

static void variant_fini(struct eu_metadata *metadata, void *value)
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

static void variant_object_iter_init(struct eu_value val,
				     struct eu_object_iter *iter)
{
	struct eu_variant *var = val.value;
	eu_object_iter_init(iter, eu_variant_peek(var));
}

struct eu_metadata eu_variant_metadata = {
	EU_JSON_VARIANT,
	sizeof(struct eu_variant),
	variant_parse,
	variant_fini,
	variant_get,
	variant_object_iter_init
};
