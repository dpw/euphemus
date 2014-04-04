#include <euphemus.h>
#include "euphemus_int.h"

static enum eu_result null_parse(const struct eu_metadata *metadata,
				 struct eu_parse *ep, void *result)
{
	struct expect expect = EXPECT_INIT(3, 'ull', "ull");

	for (;;) {
		switch (*ep->input) {
		case 'n':
			ep->input++;
			return eu_parse_expect(ep, expect);

		case WHITESPACE_CASES: {
			enum eu_result res
				= eu_consume_whitespace(metadata, ep, result);
			if (res != EU_OK)
				return res;

			break;
		}

		default:
			return EU_ERROR;
		}
	}
}

const struct eu_metadata eu_null_metadata = {
	EU_JSON_NULL,
	0,
	null_parse,
	eu_generate_fail,
	eu_noop_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size_fail
};

enum eu_result eu_variant_n(const void *null_metadata, struct eu_parse *ep,
			    struct eu_variant *result)
{
	struct expect expect = EXPECT_INIT(3, 'ull', "ull");

	result->metadata = null_metadata;
	ep->input++;
	return eu_parse_expect(ep, expect);
}
