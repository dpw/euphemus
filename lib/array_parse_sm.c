/* This is the array parsing state machine.  It is not a
   self-contained C file: it gets included in a couple of places in
   array.c */

#ifndef RESUME
#define RESUME_ONLY(x)
	state = ARRAY_PARSE_OPEN;
#else
#define RESUME_ONLY(x) x
 case ARRAY_PARSE_OPEN:
#endif
	ep->input = skip_whitespace(ep->input, ep->input_end);
	if (ep->input == ep->input_end)
		goto pause;

	if (*ep->input == ']')
		goto empty;

	el = result->a = malloc(el_size * capacity);
	memset(el, 0, el_size * capacity);
	if (!el)
		goto error;

	for (;;) {
		state = ARRAY_PARSE_ELEMENT;
		len++;
		switch (el_metadata->parse(el_metadata, ep, el)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause_in_element;

		default:
			goto error;
		}

		el += el_size;

RESUME_ONLY(case ARRAY_PARSE_ELEMENT:)
		ep->input = skip_whitespace(ep->input, ep->input_end);
		if (ep->input == ep->input_end)
			goto pause;

		switch (*ep->input) {
		case ',':
			break;

		case ']':
			goto done;

		default:
			goto error;
		}

		ep->input++;
		state = ARRAY_PARSE_COMMA;
RESUME_ONLY(case ARRAY_PARSE_COMMA:)
		if (ep->input == ep->input_end)
			goto pause;

		if (len == capacity) {
			size_t sz = capacity * el_size;
			char *new_a = realloc(result->a, sz * 2);

			capacity *= 2;
			if (new_a) {
				result->a = new_a;
				el = new_a + len * el_size;
				memset(new_a + sz, 0, sz);
			}
			else {
				free(result->a);
				result->a = NULL;
				goto error;
			}
		}
	}

 done:
	ep->input++;
	result->len = len;
	return EU_OK;

 empty:
	ep->input++;
	result->a = ZERO_LENGTH_PTR;
	return EU_OK;

 pause:
	eu_stack_begin_pause(&ep->stack);

 pause_in_element:
	frame = eu_stack_alloc(&ep->stack, sizeof *frame);
	if (frame) {
		frame->base.resume = array_parse_resume;
		frame->base.destroy = array_parse_frame_destroy;
		frame->state = state;
		frame->el_metadata = el_metadata;
		frame->result = result;
		frame->capacity = capacity;
		result->len = len;
		return EU_PAUSED;
	}

 error:
	return EU_ERROR;

#undef RESUME_ONLY

