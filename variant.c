#include "euphemus.h"
#include "euphemus_int.h"

static struct eu_metadata *json_type_metadata[EU_JSON_MAX] = {
	[EU_JSON_STRING] = &eu_string_metadata,
	[EU_JSON_OBJECT]
		= (struct eu_metadata *)&eu_inline_open_struct_metadata,
};

static unsigned char char_json_types[256] = {
	['\"'] = EU_JSON_STRING,
	['{'] = EU_JSON_OBJECT,
};

static enum eu_parse_result variant_parse(struct eu_metadata *metadata,
					 struct eu_parse *ep,
					 void *v_result)
{
	unsigned char json_type;
	struct eu_variant *result = v_result;

	json_type = char_json_types[(unsigned char)*ep->input];
	metadata = json_type_metadata[json_type];
	if (json_type == EU_JSON_INVALID)
		goto error;

	result->metadata = metadata;
	return metadata->parse(metadata, ep, &result->u);

 error:
	return EU_PARSE_ERROR;
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

struct eu_variant *eu_variant_get(struct eu_variant *variant, const char *name)
{
	if (eu_variant_type(variant) == EU_JSON_OBJECT)
		return eu_open_struct_get(&variant->u.object, name);

	abort();
}

struct eu_metadata eu_variant_metadata = {
	EU_METADATA_BASE_INITIALIZER,
	variant_parse,
	variant_fini,
	sizeof(struct eu_variant),
	EU_JSON_INVALID
};
