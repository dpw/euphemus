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

			if (*p == '\"')
				break;
		}

		member_metadata = lookup_member(metadata, result, ep->input,
						p, &member_value);
RESUME_ONLY(looked_up_member:)
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
		case EU_PARSE_OK:
			break;

		case EU_PARSE_PAUSED:
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
	return EU_PARSE_OK;

 pause_in_member_name:
	state = STRUCT_PARSE_IN_MEMBER_NAME;
	if (!eu_parse_set_member_name(ep, ep->input, p))
		goto alloc_error;

 pause:
	ep->input = p;
 pause_input_set:
	cont = malloc(sizeof *cont);
	if (!cont)
		goto alloc_error;

	cont->base.resume = struct_parse_resume;
	cont->base.destroy = struct_parse_cont_destroy;
	cont->state = state;
	cont->metadata = metadata;
	cont->result = result;
	cont->result_ptr = result_ptr;
	cont->member_metadata = member_metadata;
	cont->member_value = member_value;
	eu_parse_insert_cont(ep, &cont->base);
	return EU_PARSE_PAUSED;

 alloc_error:
 error:
	ep->input = p;
 error_input_set:
	if (result_ptr)
		*result_ptr = NULL;

	return EU_PARSE_ERROR;
