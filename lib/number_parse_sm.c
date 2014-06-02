/* This is the number parsing state machine.  It is not a
   self-contained C file: it gets included in a couple of places in
   number.c */

#ifndef RESUME
#define RESUME_ONLY(x)

 leading_minus:
	negate = -1;
	int_value = 0;
	p++;
	state = NUMBER_PARSE_LEADING_MINUS;
#else
#define RESUME_ONLY(x) x
 case NUMBER_PARSE_LEADING_MINUS:
#endif
	if (p == end)
		goto pause;

	switch (*p) {
	case '0':
		break;

	case ONE_TO_9:
		int_value = *p - '0';
		goto int_digits;

	default:
		goto error;
	}

#ifndef RESUME
 leading_zero:
	negate = 0;
	int_value = 0;
#endif
	p++;
	state = NUMBER_PARSE_LEADING_ZERO;
RESUME_ONLY(case NUMBER_PARSE_LEADING_ZERO:)
	if (p == end)
		goto pause;

	switch (*p) {
	case '.':
		goto point;

	case 'e':
	case 'E':
		goto e;

	default:
		if (metadata->integer)
			*(eu_integer_t *)result = 0;
		else
			*(eu_number_t *)result = 0;

		goto done;
	}

 int_digits:
	p++;
	state = NUMBER_PARSE_INT_DIGITS;

#ifndef RESUME
	/* 18 decimal digits is always less than 2^63.  So we can
	   process 18 digits without checking for overflow. The resume
	   case is excluded from this fast path - it's probably not
	   worth it.  We have read one digit already, so 17 left. */
	if (end - p >= 17) {
		int i;

		for (i = 0; i < 17; i++) {
			if (!(*p >= '0' && *p <= '9'))
				goto done_int_digits;

			int_value = int_value * 10 + (*p++ - '0');
		}
	}
#endif

RESUME_ONLY(case NUMBER_PARSE_INT_DIGITS:)
	for (;;) {
		if (p == end)
			goto pause;

		if (!(*p >= '0' && *p <= '9'))
			break;

		if (int_value > (UINT64_MAX-9)/10)
			goto overflow_digits;

		int_value = int_value * 10 + (*p++ - '0');
	}

#ifndef RESUME
 done_int_digits:
#endif
	switch (*p) {
	case '.':
		goto point;

	case 'e':
	case 'E':
		goto e;

	default:
		{
			/* Note that negate is 0 or -1, and that
			   int_value is non-zero. */
			if (metadata->integer) {
				int_value += negate;
				if (int_value > INT64_MAX)
					/* Overflows an int64_t */
					goto error;

				*(eu_integer_t *)result = int_value ^ negate;
			}
			else {
				*(eu_number_t *)result
					= !negate ? (eu_number_t)int_value
					          : -(eu_number_t)int_value;
			}
		}

		goto done;
	}

 overflow_digits:
	p++;
	state = NUMBER_PARSE_OVERFLOW_DIGITS;
RESUME_ONLY(case NUMBER_PARSE_OVERFLOW_DIGITS:)
	if (p == end)
		goto pause;

	switch (*p) {
	case ZERO_TO_9:
		goto overflow_digits;

	case '.':
		goto point;

	case 'e':
	case 'E':
		goto e;

	default:
		goto convert;
	}

 point:
	p++;
	state = NUMBER_PARSE_POINT;
RESUME_ONLY(case NUMBER_PARSE_POINT:)
	if (p == end)
		goto pause;

	switch (*p) {
	case ZERO_TO_9:
		break;

	default:
		goto error;
	}

	p++;
	state = NUMBER_PARSE_FRAC_DIGITS;
RESUME_ONLY(case NUMBER_PARSE_FRAC_DIGITS:)
	for (;;) {
		if (p == end)
			goto pause;

		switch (*p) {
		case ZERO_TO_9:
			break;

		case 'e':
		case 'E':
			goto e;

		default:
			goto convert;
		}

		p++;
	}

 e:
	p++;
	state = NUMBER_PARSE_E;
RESUME_ONLY(case NUMBER_PARSE_E:)
	if (p == end)
		goto pause;

	switch (*p) {
	case '+':
	case '-':
	case ZERO_TO_9:
		break;

	default:
		goto error;
	}

	p++;
	state = NUMBER_PARSE_E_DIGITS;
RESUME_ONLY(case NUMBER_PARSE_E_DIGITS:)

	for (;;) {
		if (p == end)
			goto pause;

		switch (*p) {
		case ZERO_TO_9:
			break;

		default:
			goto convert;
		}

		p++;
	}

 pause:
#ifndef RESUME
	if (!eu_stack_set_scratch(&ep->stack, ep->input, p))
#else
	if (!eu_stack_append_scratch(&ep->stack, ep->input, p))
#endif
		goto error;

	ep->input = p;

	frame = eu_stack_alloc_first(&ep->stack, sizeof *frame);
	if (!frame)
		goto error;

	frame->base.resume = number_parse_resume;
	frame->base.destroy = eu_stack_frame_noop_destroy;
	frame->metadata = metadata;
	frame->result = result;
	frame->int_value = int_value;
	frame->state = state;
	frame->negate = negate;
	return EU_PAUSED;

 error:
	ep->input = p;
	return EU_ERROR;

#undef RESUME_ONLY
#undef RESUME
