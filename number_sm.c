/* This is the number parsing state machine.  It is not a
   self-contained C file: it gets included in a couple of places in
   number.c */

#ifndef RESUME
#define RESUME_ONLY(x)

 leading_minus:
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
		negate = 1;
		goto int_digits;

	default:
		goto error;
	}

#ifndef RESUME
 leading_zero:
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
		*result = 0;
		goto done;
	}

 int_digits:
	p++;
	state = NUMBER_PARSE_INT_DIGITS;
RESUME_ONLY(case NUMBER_PARSE_INT_DIGITS:)
	if (p == end)
		goto pause;

	switch (*p) {
	case ZERO_TO_9:
		int_value = int_value * 10 + (*p - '0');
		goto int_digits;

	case '.':
		break;

	case 'e':
	case 'E':
		goto e;

	default:
		/* *result = negate ? -int_value : int_value; */
		*result = (int_value ^ -negate) + negate;
		goto done;
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
	if (!eu_parse_set_buffer(ep, ep->input, p))
		goto error;

	ep->input = p;

	cont = malloc(sizeof *cont);
	if (!cont)
		goto error;

	cont->base.resume = number_parse_resume;
	cont->base.destroy = number_parse_cont_destroy;
	cont->state = state;
	cont->negate = negate;
	cont->int_value = int_value;
	cont->result = result;
	eu_parse_insert_cont(ep, &cont->base);
	return EU_PARSE_PAUSED;

 error:
	ep->input = p;
	return EU_PARSE_ERROR;

#undef RESUME_ONLY
