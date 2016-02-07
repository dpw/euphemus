/* This is the number parsing state machine.  It is not a
   self-contained C file: it gets included in a couple of places in
   number.c */

#ifndef RESUME
#define RESUME_ONLY(x)
#else
#define RESUME_ONLY(x) x
#endif

	state = NUMBER_PARSE_START;
RESUME_ONLY(case NUMBER_PARSE_START:)

	for (;;) {
		switch (*p) {
		case '-':
			goto leading_minus;

		case '0':
			goto leading_zero;

		case ONE_TO_9:
			goto int_digits;

		case WHITESPACE_CASES: {
			p = ep->input = skip_whitespace(p, end);
			if (p == end)
				goto pause_whitespace;

			break;
		}

		default:
			goto error;
		}
	}

 leading_minus:
	negate = -1;
	p++;
	state = NUMBER_PARSE_LEADING_MINUS;
RESUME_ONLY(case NUMBER_PARSE_LEADING_MINUS:)
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

 leading_zero:
	negate = 0;
	int_value = 0;
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
		ep->input = p;
		res.type = PARSED_INTEGER;
		res.u.integer = 0;
		return res;
	}

 int_digits:
	int_value = *p++ - '0';
	state = NUMBER_PARSE_INT_DIGITS;

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

 done_int_digits:
	switch (*p) {
	case '.':
		goto point;

	case 'e':
	case 'E':
		goto e;

	default:
		/* Note that negate is 0 or -1, and that int_value is
		   non-zero. */
		int_value += negate;
		if (int_value <= INT64_MAX) {
			res.type = PARSED_INTEGER;
			res.u.integer = int_value ^ negate;
			ep->input = p;
		}
		else {
			res.type = PARSED_HUGE_INTEGER;
			res.u.p = p;
		}

		return res;
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
		res.type = PARSED_HUGE_INTEGER;
		res.u.p = p;
		return res;
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
			res.type = PARSED_NON_INTEGER;
			res.u.p = p;
			return res;
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
	case ZERO_TO_9:
		goto e_digits;

	case '+':
	case '-':
		break;

	default:
		goto error;
	}

	p++;
	state = NUMBER_PARSE_E_PLUS_MINUS;
RESUME_ONLY(case NUMBER_PARSE_E_PLUS_MINUS:)
	if (p == end)
		goto pause;

	switch (*p) {
	case ZERO_TO_9:
		goto e_digits;

	default:
		goto error;
	}

e_digits:
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
			res.type = PARSED_NON_INTEGER;
			res.u.p = p;
			return res;
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

 pause_whitespace:
	{
		struct number_parse_frame *frame
			= eu_stack_alloc_first(&ep->stack, sizeof *frame);
		if (!frame)
			goto error;

		frame->base.destroy = eu_stack_frame_noop_destroy;
		frame->int_value = int_value;
		frame->state = state;
		frame->negate = negate;

		res.type = PAUSE;
		res.u.frame = frame;
		return res;
	}

 error:
	ep->input = p;
	res.type = ERROR;
	return res;

#undef RESUME_ONLY
#undef RESUME
