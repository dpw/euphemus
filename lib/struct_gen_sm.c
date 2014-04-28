/* This is the object JSON generation state machine.  It is not a
   self-contained C file: it gets included in a couple of places in
   struct.c */

	for (;;) {
		/* TODO: escaping */

		state = STRUCT_GEN_COMMA;
RESUME_ONLY(case STRUCT_GEN_COMMA:)
		if (eg->output == eg->output_end)
			goto pause_first;

		*eg->output++ = '\"';
		state = STRUCT_GEN_IN_MEMBER_NAME;
RESUME_ONLY(case STRUCT_GEN_IN_MEMBER_NAME:)
		if (eg->output == eg->output_end)
			goto pause_first;

		state = STRUCT_GEN_MEMBER_NAME;
		/* The name is always the first field in the member struct */
		switch (name_generate(eg, *(struct eu_string_ref *)member)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause;

		default:
			goto error;
		}

RESUME_ONLY(case STRUCT_GEN_MEMBER_NAME:)
		if (eg->output == eg->output_end)
			goto pause_first;

		*eg->output++ = ':';
		state = STRUCT_GEN_COLON;
RESUME_ONLY(case STRUCT_GEN_COLON:)
		if (eg->output == eg->output_end)
			goto pause_first;

		state = STRUCT_GEN_MEMBER_VALUE;
		switch (extra_md->generate(extra_md, eg,
				     member + md->extra_member_value_offset)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause;

		default:
			goto error;
		}

RESUME_ONLY(case STRUCT_GEN_MEMBER_VALUE:)
		if (eg->output == eg->output_end)
			goto pause_first;

		if (++i == extras->len)
			break;

		*eg->output++ = ',';
		member += md->extra_member_size;
	}

	*eg->output++ = '}';
	return EU_OK;

 pause_first:
	eu_stack_begin_pause(&eg->stack);

 pause:
	frame = eu_stack_alloc(&eg->stack, sizeof *frame);
	if (!frame)
		goto alloc_error;

	frame->base.resume = struct_gen_resume;
	frame->base.destroy = eu_stack_frame_noop_destroy;
	frame->md = md;
	frame->value = value;
	frame->i = i;
	frame->state = state;
	return EU_PAUSED;

 alloc_error:
 error:
	return EU_ERROR;

#undef RESUME_ONLY
