/* This is the object JSON generation state machine.  It is not a
   self-contained C file: it gets included in a couple of places in
   array.c */

	for (;;) {
		state = ARRAY_GEN_COMMA;
RESUME_ONLY(case ARRAY_GEN_COMMA:)
		if (eg->output == eg->output_end)
			goto pause_first;

		state = ARRAY_GEN_ELEMENT;
		switch (el_md->generate(el_md, eg, el)) {
		case EU_OK:
			break;

		case EU_PAUSED:
			goto pause;

		default:
			goto error;
		}

RESUME_ONLY(case ARRAY_GEN_ELEMENT:)
		if (eg->output == eg->output_end)
			goto pause_first;

		if (!--i)
			break;

		*eg->output++ = ',';
		el += el_md->size;
	}

	*eg->output++ = ']';
	return EU_OK;

 pause_first:
	eu_stack_begin_pause(&eg->stack);

 pause:
	frame = eu_stack_alloc(&eg->stack, sizeof *frame);
	if (!frame)
		goto alloc_error;

	frame->base.resume = array_gen_resume;
	frame->base.destroy = eu_stack_frame_noop_destroy;
	frame->i = i;
	frame->el = el;
	frame->el_md = el_md;
	frame->state = state;
	return EU_PAUSED;

 alloc_error:
 error:
	return EU_ERROR;

#undef RESUME_ONLY
