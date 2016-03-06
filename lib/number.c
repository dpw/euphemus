#include <stdio.h>
#include <math.h>
#include <errno.h>

#include <euphemus.h>
#include "euphemus_int.h"

#define ONE_TO_9                                                      \
	'1': case '2': case '3': case '4': case '5':                  \
	case '6': case '7': case '8': case '9'

#define ZERO_TO_9 '0': case ONE_TO_9

enum number_parse_state {
	NUMBER_PARSE_START,
	NUMBER_PARSE_LEADING_MINUS,
	NUMBER_PARSE_LEADING_ZERO,
	NUMBER_PARSE_INT_DIGITS,
	NUMBER_PARSE_OVERFLOW_DIGITS,
	NUMBER_PARSE_POINT,
	NUMBER_PARSE_FRAC_DIGITS,
	NUMBER_PARSE_E,
	NUMBER_PARSE_E_PLUS_MINUS,
	NUMBER_PARSE_E_DIGITS,
};

struct number_parse_frame {
	struct eu_stack_frame base;
	void *result;
	uint64_t int_value;
	enum number_parse_state state;
	signed char negate;
};

struct number_parse_result {
	enum {
		PARSED_INTEGER,
		PARSED_HUGE_INTEGER,
		PARSED_NON_INTEGER,
		PAUSE,
		ERROR
	} type;
	union {
		eu_integer_t integer;
		const char *p;
		struct number_parse_frame *frame;
	} u;
};

static struct number_parse_result number_parse(struct eu_parse *ep)
{
	const char *p = ep->input;
	const char *end = ep->input_end;
	enum number_parse_state state;
	signed char negate = 0;
	uint64_t int_value = 0;
	struct number_parse_result res;

#include "number_parse_sm.c"
}

static struct number_parse_result number_parse_resume(
					  struct number_parse_frame *frame,
					  struct eu_parse *ep)
{
	enum number_parse_state state = frame->state;
	signed char negate = frame->negate;
	uint64_t int_value = frame->int_value;
	const char *p = ep->input;
	const char *end = ep->input_end;
	struct number_parse_result res;

#define RESUME
	switch (state) {
#include "number_parse_sm.c"
	}

	/* Without -O, gcc incorrectly reports that control can reach
	   here. */
	abort();
}

static enum eu_result int_parse_resume(struct eu_stack_frame *gframe,
				       void *ep);

static enum eu_result int_parse(const struct eu_metadata *metadata,
				struct eu_parse *ep, void *result)
{
	struct number_parse_result res = number_parse(ep);

	(void)metadata;

	if (res.type == PARSED_INTEGER) {
		*(eu_integer_t *)result = res.u.integer;
		return EU_OK;
	}
	else if (res.type == PAUSE) {
		res.u.frame->base.resume = int_parse_resume;
		res.u.frame->result = result;
		return EU_PAUSED;
	}
	else {
		return EU_ERROR;
	}
}

static enum eu_result int_parse_resume(struct eu_stack_frame *gframe,
				       void *ep)
{
	struct number_parse_frame *frame = (struct number_parse_frame *)gframe;
	void *result = frame->result;
	struct number_parse_result res = number_parse_resume(frame, ep);

	if (res.type == PARSED_INTEGER) {
		*(eu_integer_t *)result = res.u.integer;
		return EU_OK;
	}
	else if (res.type == PAUSE) {
		res.u.frame->base.resume = int_parse_resume;
		res.u.frame->result = result;
		return EU_PAUSED;
	}
	else {
		return EU_ERROR;
	}
}

static int convert(struct eu_parse *ep, const char *start,
		   const char *end, double *result)
{
	char *strtod_end;
	double val;

	if (!eu_locale_c(&ep->locale))
		return 0;

	/* We are running strtod here on a string that is not
	   necessarily NUL-terminated (start and end may point into
	   the input buffer), which might seem dangerous.  But it's
	   ok: If we didn't pause, then the number parsing state
	   machine guarantees that we have a character terminating the
	   number, so strtod will stop before the end of the string.
	   If we did pause, then start and end point into the scratch
	   area, and the string is NUL-terminated. */

	errno = 0;
	val = strtod(start, &strtod_end);
	if (strtod_end != end)
		/* We have already checked the syntax of the number,
		   so this should never happen. */
		abort();

	if ((val == HUGE_VAL || val == -HUGE_VAL)
	    && errno == ERANGE)
		return 0;

	*result = val;
	return 1;
}

static enum eu_result nonint_parse_resume(struct eu_stack_frame *gframe,
					  void *v_ep);

static enum eu_result nonint_parse(const struct eu_metadata *metadata,
				   struct eu_parse *ep, void *result)
{
	struct number_parse_result res = number_parse(ep);

	(void)metadata;

	switch (res.type) {
	case PARSED_INTEGER:
		*(double *)result = res.u.integer;
		return EU_OK;

	case PARSED_HUGE_INTEGER:
	case PARSED_NON_INTEGER:
		if (convert(ep, ep->input, res.u.p, result)) {
			ep->input = res.u.p;
			return EU_OK;
		}
		else {
			return EU_ERROR;
		}

	case PAUSE:
		res.u.frame->base.resume = nonint_parse_resume;
		res.u.frame->result = result;
		return EU_PAUSED;

	case ERROR:
		return EU_ERROR;
	}

	abort();
}

static enum eu_result nonint_parse_resume(struct eu_stack_frame *gframe,
					  void *v_ep)
{
	struct number_parse_frame *frame = (struct number_parse_frame *)gframe;
	struct eu_parse *ep = v_ep;
	void *result = frame->result;
	struct number_parse_result res = number_parse_resume(frame, ep);

	switch (res.type) {
	case PARSED_INTEGER:
		*(double *)result = res.u.integer;
		return EU_OK;

	case PARSED_HUGE_INTEGER:
	case PARSED_NON_INTEGER:
		if (!eu_stack_append_scratch_with_nul(&ep->stack,
						      ep->input, res.u.p))
			return EU_ERROR;

		if (convert(ep, eu_stack_scratch(&ep->stack),
			    eu_stack_scratch_end(&ep->stack) - 1, result)) {
			ep->input = res.u.p;
			eu_stack_reset_scratch(&ep->stack);
			return EU_OK;
		}
		else {
			return EU_ERROR;
		}

	case PAUSE:
		res.u.frame->base.resume = nonint_parse_resume;
		res.u.frame->result = result;
		return EU_PAUSED;

	case ERROR:
		return EU_ERROR;
	}

	abort();
}

static enum eu_result variant_number_resume(struct eu_stack_frame *gframe,
					    void *v_ep);

enum eu_result eu_variant_number(const void *unused, struct eu_parse *ep,
				 struct eu_variant *variant)
{
	struct number_parse_result res = number_parse(ep);

	(void)unused;

	switch (res.type) {
	case PARSED_INTEGER:
		variant->metadata = &eu_integer_metadata;
		variant->u.integer = res.u.integer;
		return EU_OK;

	case PARSED_HUGE_INTEGER:
	case PARSED_NON_INTEGER:
		if (convert(ep, ep->input, res.u.p, &variant->u.number)) {
			variant->metadata = &eu_double_metadata;
			ep->input = res.u.p;
			return EU_OK;
		}
		else {
			return EU_ERROR;
		}

	case PAUSE:
		res.u.frame->base.resume = variant_number_resume;
		res.u.frame->result = variant;
		return EU_PAUSED;

	case ERROR:
		return EU_ERROR;
	}

	abort();
}

static enum eu_result variant_number_resume(struct eu_stack_frame *gframe,
					    void *v_ep)
{
	struct number_parse_frame *frame = (struct number_parse_frame *)gframe;
	struct eu_parse *ep = v_ep;
	struct eu_variant *variant = frame->result;
	struct number_parse_result res = number_parse_resume(frame, ep);

	switch (res.type) {
	case PARSED_INTEGER:
		variant->metadata = &eu_integer_metadata;
		variant->u.integer = res.u.integer;
		return EU_OK;

	case PARSED_HUGE_INTEGER:
	case PARSED_NON_INTEGER:
		if (!eu_stack_append_scratch_with_nul(&ep->stack,
						      ep->input, res.u.p))
			return EU_ERROR;

		if (convert(ep, eu_stack_scratch(&ep->stack),
			    eu_stack_scratch_end(&ep->stack) - 1,
			    &variant->u.number)) {
			variant->metadata = &eu_double_metadata;
			ep->input = res.u.p;
			eu_stack_reset_scratch(&ep->stack);
			return EU_OK;
		}
		else {
			return EU_ERROR;
		}

	case PAUSE:
		res.u.frame->base.resume = variant_number_resume;
		res.u.frame->result = variant;
		return EU_PAUSED;

	case ERROR:
		return EU_ERROR;
	}

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
static enum eu_result output_scratch(struct eu_generate *eg, char *p,
				     size_t len)
{
	size_t space = eg->output_end - eg->output;
	size_t pos;
	struct number_gen_frame *frame;

	if (space >= len) {
		memcpy(eg->output, p, len);
		eg->output += len;
		return EU_OK;
	}

	memcpy(eg->output, p, space);
	eg->output = eg->output_end;

	pos = p - eu_stack_scratch(&eg->stack);
	eg->stack.scratch_size = pos + len;
	frame = eu_stack_alloc_first(&eg->stack, sizeof *frame);
	frame->base.resume = number_gen_resume;
	frame->base.destroy = eu_stack_frame_noop_destroy;
	frame->pos = pos + space;
	return EU_PAUSED;
}

static enum eu_result integer_generate(const struct eu_metadata *metadata,
				       struct eu_generate *eg, void *value)
{
	int64_t ivalue = *(eu_integer_t *)value;
	uint64_t uvalue;
	char *p;
	size_t len;

	(void)metadata;

	if (ivalue == 0) {
		*eg->output++ = '0';
		return EU_OK;
	}

	if (ivalue > 0) {
		uvalue = ivalue;
	}
	else {
		*eg->output++ = '-';
		uvalue = -(uint64_t)ivalue;
	}

	/* A 63-bit integer needs at most 19 digits */
	if (!eu_stack_reserve_scratch(&eg->stack, 19))
		goto error;

	for (p = eu_stack_scratch(&eg->stack) + 19, len = 1;; len++) {
		*--p = '0' + uvalue % 10;
		uvalue /= 10;

		if (!uvalue)
			break;
	}

	return output_scratch(eg, p, len);

 error:
	return EU_ERROR;
}

#define MAX_DOUBLE_CHARS 30

static enum eu_result number_generate(const struct eu_metadata *metadata,
				      struct eu_generate *eg, void *value)
{
	double dvalue = *(double *)value;
	int64_t ivalue = (int64_t)dvalue;
	int len;
	size_t space;

	if ((double)ivalue == dvalue)
		return integer_generate(metadata, eg, &ivalue);

	if (!isfinite(dvalue))
		goto error;

	if (!eu_locale_c(&eg->locale))
		goto error;

	space = eg->output_end - eg->output;
	if (space >= MAX_DOUBLE_CHARS) {
		/* Print into the output buffer */
		len = snprintf(eg->output, MAX_DOUBLE_CHARS, "%.16g", dvalue);
		if (len < 0 || len >= MAX_DOUBLE_CHARS)
			goto error;

		eg->output += len;
		return EU_OK;
	}
	else {
		char *p;

		/* Print into the scratch space */
		if (!eu_stack_reserve_scratch(&eg->stack, MAX_DOUBLE_CHARS))
			goto error;

		p = eu_stack_scratch(&eg->stack);
		len = snprintf(p, MAX_DOUBLE_CHARS, "%.16g", dvalue);
		if (len < 0 || len >= MAX_DOUBLE_CHARS)
			goto error;

		return output_scratch(eg, p, len);
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

static struct eu_maybe_double number_to_double(struct eu_value val)
{
	struct eu_maybe_double res = { 1, *(double *)val.value };
	return res;
}

static struct eu_maybe_integer number_to_integer(struct eu_value val)
{
	double d = *(double *)val.value;
	eu_integer_t i = d;
	struct eu_maybe_integer res = { i == d, i };
	return res;
}

const struct eu_metadata eu_double_metadata = {
	EU_JSON_NUMBER,
	sizeof(double),
	nonint_parse,
	number_generate,
	eu_noop_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size_fail,
	number_to_double,
	number_to_integer,
};

static struct eu_maybe_double integer_to_double(struct eu_value val)
{
	struct eu_maybe_double res = { 1, *(eu_integer_t *)val.value };
	return res;
}

static struct eu_maybe_integer integer_to_integer(struct eu_value val)
{
	struct eu_maybe_integer res = { 1, *(eu_integer_t *)val.value };
	return res;
}

const struct eu_metadata eu_integer_metadata = {
	EU_JSON_NUMBER,
	sizeof(eu_integer_t),
	int_parse,
	integer_generate,
	eu_noop_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size_fail,
	integer_to_double,
	integer_to_integer,
};
