#include "euphemus.h"
#include "euphemus_int.h"

/* We handle leading whitespace with a fake metadata which has a a
   parse funciton that consumes the whitespace then tries
   variant_parse again. */
#define EU_JSON_WS EU_JSON_MAX

static enum eu_parse_result consume_ws(struct eu_metadata *metadata,
				       struct eu_parse *ep,
				       void *result);

static void noop_fini(struct eu_metadata *metadata, void *value)
{
	(void)metadata;
	(void)value;
}

struct eu_metadata ws_metadata = {
	consume_ws,
	noop_fini,
	0,
	EU_JSON_INVALID
};

/* Invalid bytes are also handled by dispatching through a fake
   metadata. */

static enum eu_parse_result invalid_parse(struct eu_metadata *metadata,
					  struct eu_parse *ep,
					  void *result)
{
	(void)metadata;
	(void)ep;
	(void)result;
	return EU_PARSE_ERROR;
}

struct eu_metadata invalid_metadata = {
	invalid_parse,
	noop_fini,
	0,
	EU_JSON_INVALID
};

/* Mapping from character types to metadata records. */
static struct eu_metadata *json_type_metadata[EU_JSON_MAX+1] = {
	[EU_JSON_INVALID] = &invalid_metadata,
	[EU_JSON_STRING] = &eu_string_metadata,
	[EU_JSON_OBJECT] = &eu_inline_open_struct_metadata.base,
	[EU_JSON_ARRAY] = &eu_variant_array_metadata.base,
	[EU_JSON_NUMBER] = &eu_number_metadata,
	[EU_JSON_BOOL] = &eu_bool_metadata,
	[EU_JSON_NULL] = &eu_null_metadata,
	[EU_JSON_WS] = &ws_metadata
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
	['\f'] = EU_JSON_WS,
	['\n'] = EU_JSON_WS,
	['\t'] = EU_JSON_WS,
	['\v'] = EU_JSON_WS,
};

static enum eu_parse_result variant_parse(struct eu_metadata *metadata,
					 struct eu_parse *ep,
					 void *v_result)
{
	struct eu_variant *result = v_result;
	unsigned char json_type = char_json_types[(unsigned char)*ep->input];
	result->metadata = metadata = json_type_metadata[json_type];
	return metadata->parse(metadata, ep, &result->u);
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

static enum eu_parse_result consume_ws(struct eu_metadata *metadata,
				       struct eu_parse *ep,
				       void *result)
{
	enum eu_parse_result res = eu_consume_whitespace(ep, metadata, result);
	if (res == EU_PARSE_OK)
		return variant_parse(metadata, ep,
			       (char *)result - offsetof(struct eu_variant, u));
	else
		return res;
}

struct eu_variant *eu_variant_get(struct eu_variant *variant, const char *name)
{
	if (eu_variant_type(variant) == EU_JSON_OBJECT)
		return eu_open_struct_get(&variant->u.object, name);

	abort();
}

struct eu_metadata eu_variant_metadata = {
	variant_parse,
	variant_fini,
	sizeof(struct eu_variant),
	EU_JSON_INVALID
};

struct eu_array_metadata eu_variant_array_metadata
	= EU_ARRAY_METADATA_INITIALIZER(&eu_variant_metadata);

void eu_parse_init_variant(struct eu_parse *ep, struct eu_variant *var)
{
	eu_parse_init(ep, &eu_variant_metadata, var);
}
