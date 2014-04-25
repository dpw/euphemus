/* This is the object JSON generation state machine.  It is not a
   self-contained C file: it gets included in a couple of places in
   struct.c */

	for (;;) {
		/* TODO: escaping */

		state = STRUCT_COMMA;
		if (eg->output == eg->output_end)
			goto pause;
RESUME_ONLY(case STRUCT_COMMA:)

		*eg->output++ = '\"';
		state = STRUCT_IN_MEMBER_NAME;
		if (eg->output == eg->output_end)
			goto pause;
RESUME_ONLY(case STRUCT_IN_MEMBER_NAME:)

		state = STRUCT_MEMBER_NAME;
		/* The name is always the first field in the member struct */
		switch (name_generate(eg, *(struct eu_string_ref *)member)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause;

		default:
			goto error;
		}

		if (eg->output == eg->output_end)
			goto pause;
RESUME_ONLY(case STRUCT_MEMBER_NAME:)

		*eg->output++ = ':';
		state = STRUCT_COLON;
		if (eg->output == eg->output_end)
			goto pause;
RESUME_ONLY(case STRUCT_COLON:)

		state = STRUCT_MEMBER_VALUE;
		switch (extra_md->generate(extra_md, eg,
				     member + md->extra_member_value_offset)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause;

		default:
			goto error;
		}

		if (eg->output == eg->output_end)
				goto pause;
RESUME_ONLY(case STRUCT_MEMBER_VALUE:)

		if (++i == extras->len)
			break;

		*eg->output++ = ',';
		member += md->extra_member_size;
	}

	*eg->output++ = '}';
	return EU_OK;

 pause:
 error:
	/* This is just here to act as a use of state */
	if (state == STRUCT_OPEN)
		abort();

	return EU_ERROR;
