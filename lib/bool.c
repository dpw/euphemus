#include <euphemus.h>
#include "euphemus_int.h"

static enum eu_parse_result bool_parse(const struct eu_metadata *metadata,
				       struct eu_parse *ep,
				       void *v_result)
{
	eu_bool_t *result = v_result;
	struct expect expect = EXPECT_INIT(4, 'alse', "alse");

	*result = 0;

	for (;;) {
		switch (*ep->input) {
		case 't':
			*result = 1;
			EXPECT_ASSIGN(expect, 3, 'rue', "rue");
			/* fall through */

		case 'f':
			ep->input++;
			return eu_parse_expect(ep, expect);

		case WHITESPACE_CASES: {
			enum eu_parse_result res
				= eu_consume_whitespace(metadata, ep, result);
			if (res != EU_PARSE_OK)
				return res;

			break;
		}

		default:
			return EU_PARSE_ERROR;
		}
	}
}

struct eu_metadata eu_bool_metadata = {
	EU_JSON_BOOL,
	sizeof(eu_bool_t),
	bool_parse,
	eu_generate_fail,
	eu_noop_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size
};

struct eu_bool_misc {
	eu_bool_t value;
	struct expect expect;
};

const struct eu_bool_misc eu_bool_true = { 1, EXPECT_INIT(3, 'rue', "rue") };
const struct eu_bool_misc eu_bool_false = { 0, EXPECT_INIT(4, 'alse', "alse") };

enum eu_parse_result eu_variant_bool(const void *v_misc, struct eu_parse *ep,
				     struct eu_variant *result)
{
	const struct eu_bool_misc *misc = v_misc;

	result->metadata = &eu_bool_metadata;
	ep->input++;
	result->u.bool = misc->value;
	return eu_parse_expect(ep, misc->expect);
}
