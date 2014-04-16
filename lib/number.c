#include <stdio.h>

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

struct number_parse_frame {
	struct eu_stack_frame base;
	enum number_parse_state state;
	double *result;
	int64_t int_value;
	signed char negate;
};

static enum eu_result number_parse_resume(struct eu_stack_frame *gframe,
					  void *v_ep);

static enum eu_result number_parse(const struct eu_metadata *metadata,
				   struct eu_parse *ep, void *v_result)
{
	const char *p = ep->input;
	const char *end = ep->input_end;
	double *result = v_result;
	enum number_parse_state state;
	signed char negate;
	int64_t int_value;
	struct number_parse_frame *frame;

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
			enum eu_result res
				= eu_consume_whitespace(metadata, ep, result);
			if (res != EU_OK)
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
	return EU_OK;
}

static enum eu_result number_parse_resume(struct eu_stack_frame *gframe,
					  void *v_ep)
{
	struct number_parse_frame *frame = (struct number_parse_frame *)gframe;
	struct eu_parse *ep = v_ep;
	enum number_parse_state state = frame->state;
	signed char negate = frame->negate;
	int64_t int_value = frame->int_value;
	double *result = frame->result;
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
		return EU_OK;
	}

	/* Without -O, gcc incorrectly reports that control can reach
	   here. */
	abort();
}

struct number_gen_frame {
	struct eu_stack_frame base;
	unsigned int pos;
};

static enum eu_result number_gen_resume(struct eu_stack_frame *gframe,
					void *eg);


/* 30 characters ought to be safe for an IEEE754 double, including the
   trailing 0 byte. */
#define MAX_DOUBLE_CHARS 30

static enum eu_result number_generate(const struct eu_metadata *metadata,
				      struct eu_generate *eg, void *value)
{
	double dvalue = *(double *)value;
	int64_t ivalue = (int64_t)dvalue;
	uint64_t uvalue;
	size_t space;
	char *p;
	struct number_gen_frame *frame;
	unsigned int len, scratch_size, pos;

	(void)metadata;

	if ((double)ivalue != dvalue)
		goto non_integer;

	if (ivalue == 0) {
		*eg->output++ = '0';
		return EU_OK;
	}

	if (ivalue > 0) {
		uvalue = ivalue;
	}
	else {
		*eg->output++ = '-';
		uvalue = -ivalue;
	}

	/* A 63-bit integer needs at most 19 digits */
	scratch_size = 19;
	if (!eu_stack_reserve_scratch(&eg->stack, scratch_size))
		goto error;

	for (p = eu_stack_scratch(&eg->stack) + scratch_size, len = 1;; len++) {
		*--p = '0' + uvalue % 10;
		uvalue /= 10;

		if (!uvalue)
			break;
	}

 output_scratch:
	space = eg->output_end - eg->output;
	if (space >= len) {
		memcpy(eg->output, p, len);
		eg->output += len;
		return EU_OK;
	}

	memcpy(eg->output, p, space);
	eg->output = eg->output_end;
	pos = p + space - eu_stack_scratch(&eg->stack);

	eg->stack.scratch_size = scratch_size;
	frame = eu_stack_alloc_first(&eg->stack, sizeof *frame);
	frame->base.resume = number_gen_resume;
	frame->base.destroy = eu_stack_frame_noop_destroy;
	frame->pos = pos;
	return EU_PAUSED;

 non_integer:
	space = eg->output_end - eg->output;
	if (space >= MAX_DOUBLE_CHARS) {
		/* Print into the output buffer */
		int plen = snprintf(eg->output, MAX_DOUBLE_CHARS, "%.16g",
				    dvalue);
		if (plen < 0 || plen >= MAX_DOUBLE_CHARS)
			goto error;

		eg->output += plen;
		return EU_OK;
	}
	else {
		/* Print into the scratch space */
		int plen;

		if (!eu_stack_reserve_scratch(&eg->stack, MAX_DOUBLE_CHARS))
			goto error;

		p = eu_stack_scratch(&eg->stack);
		plen = snprintf(p, MAX_DOUBLE_CHARS, "%.16g", dvalue);
		if (plen < 0 || plen >= MAX_DOUBLE_CHARS)
			goto error;

		scratch_size = len = plen;
		goto output_scratch;
	}

 error:
	return EU_ERROR;
}

static enum eu_result number_gen_resume(struct eu_stack_frame *gframe,
					void *v_eg)
{
	struct number_gen_frame *frame = (struct number_gen_frame *)gframe;
	struct eu_generate *eg = v_eg;
	size_t space = eg->output_end - eg->output;
	size_t len = eg->stack.scratch_size - frame->pos;
	char *p = eu_stack_scratch(&eg->stack) + frame->pos;

	if (space >= len) {
		memcpy(eg->output, p, len);
		eg->output += len;
		eu_stack_reset_scratch(&eg->stack);
		return EU_OK;
	}

	memcpy(eg->output, p, space);
	eg->output = eg->output_end;
	frame->pos += space;
	return EU_REINSTATE_PAUSED;
}

const struct eu_metadata eu_number_metadata = {
	EU_JSON_NUMBER,
	sizeof(double),
	number_parse,
	number_generate,
	eu_noop_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size_fail
};

enum eu_result eu_variant_number(const void *number_metadata,
				 struct eu_parse *ep, struct eu_variant *result)
{
	result->metadata = number_metadata;
	return number_parse(number_metadata, ep, &result->u.number);
}
