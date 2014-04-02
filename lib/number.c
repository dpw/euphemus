#include <stdint.h>

#include <euphemus.h>
#include "euphemus_int.h"

#define ONE_TO_9                                                      \
	'1': case '2': case '3': case '4': case '5':                  \
	case '6': case '7': case '8': case '9'

#define ZERO_TO_9 '0': case ONE_TO_9

enum number_parse_state {
	NUMBER_PARSE_LEADING_MINUS,
	NUMBER_PARSE_LEADING_ZERO,
	NUMBER_PARSE_INT_DIGITS,
	NUMBER_PARSE_OVERFLOW_DIGITS,
	NUMBER_PARSE_POINT,
	NUMBER_PARSE_FRAC_DIGITS,
	NUMBER_PARSE_E,
	NUMBER_PARSE_E_DIGITS,
};

struct number_parse_cont {
	struct eu_parse_cont base;
	enum number_parse_state state;
	signed char negate;
	double *result;
	int64_t int_value;
};

static enum eu_parse_result number_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont);

static enum eu_parse_result number_parse(const struct eu_metadata *metadata,
					 struct eu_parse *ep,
					 void *v_result)
{
	const char *p = ep->input;
	const char *end = ep->input_end;
	double *result = v_result;
	enum number_parse_state state;
	signed char negate;
	int64_t int_value;
	struct number_parse_cont *cont;

	(void)metadata;

	for (;;) {
		switch (*p) {
		case '-':
			goto leading_minus;

		case '0':
			goto leading_zero;

		case ONE_TO_9:
			int_value = *p - '0';
			negate = 0;
			goto int_digits;

		case WHITESPACE_CASES: {
			enum eu_parse_result res
				= eu_consume_whitespace(metadata, ep, result);
			if (res != EU_PARSE_OK)
				return res;

			p = ep->input;
			break;
		}

		default:
			goto error;
		}
	}

#include "number_sm.c"

 convert:
	{
		char *strtod_end;
		*result = strtod(ep->input, &strtod_end);
		if (strtod_end != p)
			abort();
	}

 done:
	ep->input = p;
	return EU_PARSE_OK;
}

static enum eu_parse_result number_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont)
{
	struct number_parse_cont *cont = (struct number_parse_cont *)gcont;
	enum number_parse_state state = cont->state;
	signed char negate = cont->negate;
	int64_t int_value = cont->int_value;
	double *result = cont->result;
	const char *p = ep->input;
	const char *end = ep->input_end;

#define RESUME
	switch (state) {
#include "number_sm.c"

	convert:
		{
			char *strtod_end;

			if (!eu_stack_append_scratch_with_nul(&ep->stack,
							      ep->input, p))
				goto error;

			*result = strtod(eu_stack_scratch(&ep->stack),
					 &strtod_end);
			if (strtod_end != eu_stack_scratch_end(&ep->stack) - 1)
				abort();

			eu_stack_reset_scratch(&ep->stack);
		}

	done:
		ep->input = p;
		return EU_PARSE_OK;
	}

	/* Without -O, gcc incorrectly reports that control can reach
	   here. */
	abort();
}

const struct eu_metadata eu_number_metadata = {
	EU_JSON_NUMBER,
	sizeof(double),
	number_parse,
	eu_generate_fail,
	eu_noop_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size_fail
};

enum eu_parse_result eu_variant_number(const void *number_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result)
{
	result->metadata = number_metadata;
	return number_parse(number_metadata, ep, &result->u.number);
}
