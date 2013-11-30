#include "euphemus.h"
#include "euphemus_int.h"

enum char_type {
	CHAR_TYPE_NONE,
	CHAR_TYPE_STRING,
	CHAR_TYPE_MAX
};

static struct eu_metadata *char_type_metadata[CHAR_TYPE_MAX] = {
	[CHAR_TYPE_STRING] = &eu_string_metadata
};

static unsigned char char_types[256] = {
	['\"'] = CHAR_TYPE_STRING
};

static enum eu_parse_result variant_parse(struct eu_metadata *metadata,
					 struct eu_parse *ep,
					 void *v_result)
{
	unsigned char char_type;
	struct eu_variant *result = v_result;

	char_type = char_types[(unsigned char)*ep->input];
	metadata = char_type_metadata[char_type];
	if (char_type == CHAR_TYPE_NONE)
		goto error;

	result->metadata = metadata;
	return metadata->parse(metadata, ep, &result->u);

 error:
	return EU_PARSE_ERROR;
}

static void variant_destroy(struct eu_metadata *metadata, void *value)
{
	struct eu_variant *var = value;
	(void)metadata;

	if (var->metadata)
		var->metadata->destroy(var->metadata, &var->u);
}

struct eu_metadata eu_variant_metadata = {
	EU_METADATA_BASE_INITIALIZER,
	variant_parse,
	variant_destroy,
	sizeof(struct eu_variant)
};
