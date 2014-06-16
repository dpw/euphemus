#include <euphemus.h>
#include "euphemus_int.h"

static enum eu_result bool_parse(const struct eu_metadata *metadata,
				 struct eu_parse *ep, void *v_result)
{
	eu_bool_t *result = v_result;
	struct expect expect
		= EXPECT_INIT(4, MULTICHAR_4('a','l','s','e'), "alse");

	*result = 0;

	for (;;) {
		switch (*ep->input) {
		case 't':
			*result = 1;
			EXPECT_ASSIGN(expect, 3,
				      MULTICHAR_3('r','u','e'), "rue");
			/* fall through */

		case 'f':
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

static struct fixed_gen_64 bool_fixed_gens[2] = {
	FIXED_GEN_64_INIT(4, MULTICHAR_4('t','r','u','e'), "true"),
	FIXED_GEN_64_INIT(5, MULTICHAR_5('f','a','l','s','e'), "false")
};

static enum eu_result bool_generate(const struct eu_metadata *metadata,
				    struct eu_generate *eg, void *v_value)
{
	eu_bool_t *value = v_value;

	(void)metadata;

	return eu_fixed_gen_64(eg, bool_fixed_gens[!*value]);
}

const struct eu_metadata eu_bool_metadata = {
	EU_JSON_BOOL,
	sizeof(eu_bool_t),
	bool_parse,
	bool_generate,
	eu_noop_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size,
	eu_to_double_fail,
	eu_to_integer_fail,
};

struct eu_bool_misc {
	eu_bool_t value;
	struct expect expect;
};

const struct eu_bool_misc eu_bool_true
	= { 1, EXPECT_INIT(3, MULTICHAR_3('r','u','e'), "rue") };
const struct eu_bool_misc eu_bool_false
	= { 0, EXPECT_INIT(4, MULTICHAR_4('a','l','s','e'), "alse") };

enum eu_result eu_variant_bool(const void *v_misc, struct eu_parse *ep,
			       struct eu_variant *result)
{
	const struct eu_bool_misc *misc = v_misc;

	result->metadata = &eu_bool_metadata;
	ep->input++;
	result->u.bool = misc->value;
	return eu_parse_expect(ep, misc->expect);
}
