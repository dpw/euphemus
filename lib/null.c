#include <euphemus.h>
#include "euphemus_int.h"

static enum eu_result null_parse(const struct eu_metadata *metadata,
				 struct eu_parse *ep, void *result)
{
	struct expect expect = EXPECT_INIT(3, MULTICHAR_3('u','l','l'), "ull");

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

static enum eu_result null_generate(const struct eu_metadata *metadata,
				    struct eu_generate *eg, void *value)
{
	(void)metadata;
	(void)value;

	return eu_fixed_gen_32(eg, 4, MULTICHAR_4('n','u','l','l'), "null");
}

const struct eu_metadata eu_null_metadata = {
	EU_JSON_NULL,
	0,
	null_parse,
	null_generate,
	eu_noop_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size_fail,
	eu_to_double_fail,
};

enum eu_result eu_variant_n(const void *null_metadata, struct eu_parse *ep,
			    struct eu_variant *result)
{
	struct expect expect = EXPECT_INIT(3, MULTICHAR_3('u','l','l'), "ull");

	result->metadata = null_metadata;
	ep->input++;
	return eu_parse_expect(ep, expect);
}
