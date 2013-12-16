#include "euphemus.h"
#include "euphemus_int.h"

static enum eu_parse_result bool_parse(struct eu_metadata *metadata,
				       struct eu_parse *ep,
				       void *v_result)
{
	eu_bool_t *result = v_result;

	for (;;) {
		switch (*ep->input) {
		case 't':
			ep->input++;
			*result = 1;
			return eu_parse_expect(ep, "rue", 3);

		case 'f':
			ep->input++;
			*result = 0;
			return eu_parse_expect(ep, "alse", 4);

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
	bool_parse,
	eu_noop_fini,
	sizeof(eu_bool_t),
	EU_JSON_BOOL
};

void eu_parse_init_bool(struct eu_parse *ep, eu_bool_t *bool)
{
	eu_parse_init(ep, &eu_bool_metadata, bool);
}

struct eu_bool_misc {
	eu_bool_t value;
	unsigned char expect_len;
	const char *expect;
};

struct eu_bool_misc eu_bool_true = { 1, 3, "rue" };
struct eu_bool_misc eu_bool_false = { 0, 4, "alse" };

enum eu_parse_result eu_variant_bool(void *v_misc, struct eu_parse *ep,
				     struct eu_variant *result)
{
	struct eu_bool_misc *misc = v_misc;

	result->metadata = &eu_bool_metadata;
	ep->input++;
	result->u.bool = misc->value;
	return eu_parse_expect(ep, misc->expect, misc->expect_len);
}
