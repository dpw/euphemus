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
static void number_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *cont);

static enum eu_parse_result number_parse(struct eu_metadata *metadata,
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

	free(cont);

#define RESUME
#define eu_parse_set_buffer eu_parse_append_buffer
	switch (state) {
#include "number_sm.c"
#undef eu_parse_set_buffer

	convert:
		{
			char *strtod_end;

			if (!eu_parse_append_buffer_nul(ep, ep->input, p))
				goto error;

			*result = strtod(ep->buf, &strtod_end);
			if (strtod_end != ep->buf + ep->buf_len)
				abort();
		}

	done:
		ep->input = p;
		return EU_PARSE_OK;
	}

	/* Without -O, gcc incorrectly reports that control can reach
	   here. */
	abort();
}

static void number_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *cont)
{
	(void)ep;
	free(cont);
}

struct eu_metadata eu_number_metadata = {
	number_parse,
	eu_noop_fini,
	eu_get_fail,
	sizeof(double),
	EU_JSON_NUMBER,
	eu_object_iter_init_fail
};

enum eu_parse_result eu_variant_number(void *number_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result)
{
	result->metadata = number_metadata;
	return number_parse(number_metadata, ep, &result->u.number);
}

void eu_parse_init_number(struct eu_parse *ep, double *num)
{
	eu_parse_init(ep, &eu_number_metadata, num);
}
