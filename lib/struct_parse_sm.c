/* This is the object parsing state machine.  It is not a
   self-contained C file: it gets included in a couple of places in
   struct.c */

	state = STRUCT_PARSE_OPEN;
RESUME_ONLY(case STRUCT_PARSE_OPEN:)
	p = skip_whitespace(p, end);
	if (p == end)
		goto pause;

	switch (*p) {
	case '\"':
		break;

	case '}':
		goto done;

	default:
		goto error;
	}

	for (;;) {
		/* Record the start of the member name */
		ep->input = ++p;
		for (;; p++) {
			if (p == end)
				goto pause_in_member_name;

			switch (*p) {
			case '\"': goto member_name_done;
			case '\\': goto unescape_member_name;
			default: break;
			}
		}

	member_name_done:
		member_metadata = add_member(metadata, result, ep->input,
					     p, &member_value);
	looked_up_member:
		if (!member_metadata)
			goto error;

		p++;
		state = STRUCT_PARSE_MEMBER_NAME;
RESUME_ONLY(case STRUCT_PARSE_MEMBER_NAME:)
		p = skip_whitespace(p, end);
		if (p == end)
			goto pause;

		if (*p != ':')
			goto error;

		p++;
		state = STRUCT_PARSE_COLON;
RESUME_ONLY(case STRUCT_PARSE_COLON:)
		if (p == end)
			goto pause;

		state = STRUCT_PARSE_MEMBER_VALUE;
		ep->input = p;
		switch (member_metadata->parse(member_metadata, ep,
					       member_value)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause_input_set;

		default:
			goto error_input_set;
		}

		end = ep->input_end;
RESUME_ONLY(case STRUCT_PARSE_MEMBER_VALUE:)
		p = skip_whitespace(ep->input, end);
		if (p == end)
			goto pause;

		switch (*p) {
		case ',':
			break;

		case '}':
			goto done;

		default:
			goto error;
		}

		p++;
		state = STRUCT_PARSE_COMMA;
RESUME_ONLY(case STRUCT_PARSE_COMMA:)
		p = skip_whitespace(p, end);
		if (p == end)
			goto pause;

		if (*p != '\"')
			goto error;
	}

 done:
	ep->input = p + 1;
	return EU_OK;

 pause_in_member_name:
	state = STRUCT_PARSE_IN_MEMBER_NAME;
	if (!eu_stack_set_scratch(&ep->stack, ep->input, p))
		goto alloc_error;

 pause:
	ep->input = p;
	eu_stack_begin_pause(&ep->stack);

 pause_input_set:
	frame = eu_stack_alloc(&ep->stack, sizeof *frame);
	if (!frame)
		goto alloc_error;

	frame->base.resume = struct_parse_resume;
	frame->base.destroy = struct_parse_frame_destroy;
	frame->state = state;
	frame->metadata = metadata;
	frame->result = result;
	frame->result_ptr = result_ptr;
	frame->member_metadata = member_metadata;
	frame->member_value = member_value;
	frame->unescape = unescape;
	return EU_PAUSED;

 unescape_member_name:
	/* Skip the backslash, and scan forward to find the end of the
	   member name */
	do {
		if (++p == end)
			goto pause_unescape_member_name;
	} while (*p != '\"' || quotes_escaped(p));

	if (!eu_stack_reserve_scratch(&ep->stack, p - ep->input))
		goto error_input_set;

	{
		char *unescaped_end = eu_unescape(ep, p,
						  eu_stack_scratch(&ep->stack),
						  &unescape);
		if (!unescaped_end || unescape)
			goto error_input_set;

		member_metadata = add_member(metadata, result,
					     eu_stack_scratch(&ep->stack),
					     unescaped_end, &member_value);
		eu_stack_reset_scratch(&ep->stack);
	}

	goto looked_up_member;

pause_unescape_member_name:
	if (!eu_stack_reserve_scratch(&ep->stack, p - ep->input))
		goto error_input_set;

        {
		char *unescaped_end = eu_unescape(ep, p,
						  eu_stack_scratch(&ep->stack),
						  &unescape);
		if (!unescaped_end)
			goto error_input_set;

		eu_stack_set_scratch_end(&ep->stack, unescaped_end);
	}

	state = STRUCT_PARSE_IN_MEMBER_NAME;
	goto pause;

 alloc_error:
 error:
	ep->input = p;
 error_input_set:
	if (result_ptr) {
		free(result);
		*result_ptr = NULL;
	}

	return EU_ERROR;
