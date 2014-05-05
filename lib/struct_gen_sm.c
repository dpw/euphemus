/* This is the object JSON generation state machine.  It is not a
   self-contained C file: it gets included in a couple of places in
   struct.c */

	/* TODO: escaping */

	while (i != md->n_members) {
		if (member->presence_offset < 0) {
			if (!*(void **)((char *)value + member->offset))
				goto next;
		}
		else {
			if (!(((char *)value)[member->presence_offset]
			      & member->presence_bit))
				goto next;
		}

		*eg->output++ = prefix;
		prefix = ',';
		state = STRUCT_MEMBERS_GEN_PREFIX;
RESUME_ONLY(case STRUCT_MEMBERS_GEN_PREFIX:)
		if (eg->output == eg->output_end)
			goto pause_first;

		*eg->output++ = '\"';
		state = STRUCT_MEMBERS_GEN_IN_MEMBER_NAME;
RESUME_ONLY(case STRUCT_MEMBERS_GEN_IN_MEMBER_NAME:)
		if (eg->output == eg->output_end)
			goto pause_first;

		state = STRUCT_MEMBERS_GEN_MEMBER_NAME;
		/* The name is always the first field in the member struct */
		switch (name_generate(eg, eu_string_ref(member->name,
							member->name_len))) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause;

		default:
			goto error;
		}

RESUME_ONLY(case STRUCT_MEMBERS_GEN_MEMBER_NAME:)
		if (eg->output == eg->output_end)
			goto pause_first;

		*eg->output++ = ':';
		state = STRUCT_MEMBERS_GEN_COLON;
RESUME_ONLY(case STRUCT_MEMBERS_GEN_COLON:)
		if (eg->output == eg->output_end)
			goto pause_first;

		state = STRUCT_MEMBERS_GEN_MEMBER_VALUE;
		switch (member->metadata->generate(member->metadata, eg,
				          (char *)value + member->offset)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause;

		default:
			goto error;
		}

RESUME_ONLY(case STRUCT_MEMBERS_GEN_MEMBER_VALUE:)
		if (eg->output == eg->output_end)
			goto pause_first;

	next:
		i++;
		member++;
	}

	i = 0;

	while (i != extras->len) {
		*eg->output++ = prefix;
		prefix = ',';
		state = STRUCT_EXTRAS_GEN_PREFIX;
RESUME_ONLY(case STRUCT_EXTRAS_GEN_PREFIX:)
		if (eg->output == eg->output_end)
			goto pause_first;

		*eg->output++ = '\"';
		state = STRUCT_EXTRAS_GEN_IN_MEMBER_NAME;
RESUME_ONLY(case STRUCT_EXTRAS_GEN_IN_MEMBER_NAME:)
		if (eg->output == eg->output_end)
			goto pause_first;

		state = STRUCT_EXTRAS_GEN_MEMBER_NAME;
		/* The name is always the first field in the member struct */
		switch (name_generate(eg, *(struct eu_string_ref *)extra)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause;

		default:
			goto error;
		}

RESUME_ONLY(case STRUCT_EXTRAS_GEN_MEMBER_NAME:)
		if (eg->output == eg->output_end)
			goto pause_first;

		*eg->output++ = ':';
		state = STRUCT_EXTRAS_GEN_COLON;
RESUME_ONLY(case STRUCT_EXTRAS_GEN_COLON:)
		if (eg->output == eg->output_end)
			goto pause_first;

		state = STRUCT_EXTRAS_GEN_MEMBER_VALUE;
		switch (extra_md->generate(extra_md, eg,
				     extra + md->extra_member_value_offset)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause;

		default:
			goto error;
		}

RESUME_ONLY(case STRUCT_EXTRAS_GEN_MEMBER_VALUE:)
		if (eg->output == eg->output_end)
			goto pause_first;

		i++;
		extra += md->extra_member_size;
	}

	if (prefix != '{') {
		*eg->output++ = '}';
		return EU_OK;
	}

	/* Empty object */
	return eu_fixed_gen_32(eg, 2, MULTICHAR_2('{','}'), "{}");

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
	frame->prefix = prefix;
	return EU_PAUSED;

 alloc_error:
 error:
	return EU_ERROR;

#undef RESUME_ONLY
