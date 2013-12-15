#include "euphemus.h"
#include "euphemus_int.h"

static enum eu_parse_result invalid(struct eu_parse *ep,
				    struct eu_variant *result)
{
	(void)ep;
	(void)result;
	return EU_PARSE_ERROR;
}

static enum eu_parse_result whitespace(struct eu_parse *ep,
				       struct eu_variant *result);

typedef enum eu_parse_result (*dispatch_func)(struct eu_parse *ep,
					      struct eu_variant *result);

/* We handle leading whitespace with a fake metadata which has a a
   parse funciton that consumes the whitespace then tries
   variant_parse again. */
#define EU_JSON_WS EU_JSON_MAX

/* Mapping from character types to metadata records. */
static dispatch_func json_type_funcs[EU_JSON_MAX+1] = {
	[EU_JSON_INVALID] = invalid,
	[EU_JSON_STRING] = eu_variant_string,
	[EU_JSON_OBJECT] = eu_variant_object,
	[EU_JSON_ARRAY] = eu_variant_array,
	[EU_JSON_NUMBER] = eu_variant_number,
	[EU_JSON_BOOL] = eu_variant_bool,
	[EU_JSON_NULL] = eu_variant_null,
	[EU_JSON_WS] = whitespace
};

/* Mapping from characters to character types. */
static unsigned char char_json_types[256] = {
	['\"'] = EU_JSON_STRING,
	['{'] = EU_JSON_OBJECT,
	['['] = EU_JSON_ARRAY,

	['0'] = EU_JSON_NUMBER,
	['1'] = EU_JSON_NUMBER,
	['2'] = EU_JSON_NUMBER,
	['3'] = EU_JSON_NUMBER,
	['4'] = EU_JSON_NUMBER,
	['5'] = EU_JSON_NUMBER,
	['6'] = EU_JSON_NUMBER,
	['7'] = EU_JSON_NUMBER,
	['8'] = EU_JSON_NUMBER,
	['9'] = EU_JSON_NUMBER,
	['-'] = EU_JSON_NUMBER,

	['t'] = EU_JSON_BOOL,
	['f'] = EU_JSON_BOOL,

	['n'] = EU_JSON_NULL,

	[' '] = EU_JSON_WS,
	['\t'] = EU_JSON_WS,
	['\n'] = EU_JSON_WS,
	['\r'] = EU_JSON_WS,
};

static enum eu_parse_result variant_parse(struct eu_metadata *metadata,
					 struct eu_parse *ep,
					 void *result)
{
	unsigned char type = char_json_types[(unsigned char)*ep->input];

	(void)metadata;
	return json_type_funcs[type](ep, result);
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

static enum eu_parse_result whitespace(struct eu_parse *ep,
				       struct eu_variant *result)
{
	enum eu_parse_result res
		= eu_consume_whitespace(ep, &eu_variant_metadata, result);
	if (res == EU_PARSE_OK)
		return variant_parse(&eu_variant_metadata, ep, result);
	else
		return res;
}

struct eu_variant *eu_variant_get(struct eu_variant *variant, const char *name)
{
	if (eu_variant_type(variant) == EU_JSON_OBJECT)
		return eu_object_get(&variant->u.object, name);

	abort();
}

struct eu_metadata eu_variant_metadata = {
	variant_parse,
	variant_fini,
	sizeof(struct eu_variant),
	EU_JSON_INVALID
};

void eu_parse_init_variant(struct eu_parse *ep, struct eu_variant *var)
{
	eu_parse_init(ep, &eu_variant_metadata, var);
}
