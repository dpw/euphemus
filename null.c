#include "euphemus.h"
#include "euphemus_int.h"

static enum eu_parse_result null_parse(struct eu_metadata *metadata,
				       struct eu_parse *ep,
				       void *result)
{
	for (;;) {
		switch (*ep->input) {
		case 'n':
			ep->input++;
			return eu_parse_expect(ep, "ull", 3);

		case WHITESPACE_CASES: {
			enum eu_parse_result res
				= eu_consume_whitespace(ep, metadata, result);
			if (res != EU_PARSE_OK)
				return res;

			break;
		}

		default:
			return EU_PARSE_ERROR;
		}
	}
}

struct eu_metadata eu_null_metadata = {
	null_parse,
	eu_noop_fini,
	0,
	EU_JSON_NULL
};

enum eu_parse_result eu_variant_n(void *null_metadata, struct eu_parse *ep,
				  struct eu_variant *result)
{
	result->metadata = null_metadata;
	ep->input++;
	return eu_parse_expect(ep, "ull", 3);
}
