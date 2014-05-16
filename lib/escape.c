#include <euphemus.h>

#include "euphemus_int.h"

struct escape_frame {
	struct eu_stack_frame base;
	struct eu_string_ref str;
};

static enum eu_result escape_resume(struct eu_stack_frame *gframe, void *eg);

enum eu_result eu_escape(struct eu_generate *eg, struct eu_string_ref str)
{
	struct escape_frame *frame;
	size_t space;

	space = eg->output_end - eg->output;
	if (str.len < space) {
		/* TODO escaping */

		memcpy(eg->output, str.chars, str.len);
		eg->output += str.len;
		*eg->output++ = '\"';
		return EU_OK;
	}

	memcpy(eg->output, str.chars, space);
	eg->output += space;

	frame = eu_stack_alloc_first(&eg->stack, sizeof *frame);
	if (frame) {
		frame->base.resume = escape_resume;
		frame->base.destroy = eu_stack_frame_noop_destroy;
		frame->str.chars = str.chars + space;
		frame->str.len = str.len - space;
		return EU_PAUSED;
	}

	return EU_ERROR;
}

static enum eu_result escape_resume(struct eu_stack_frame *gframe, void *eg)
{
	struct escape_frame *frame = (struct escape_frame *)gframe;
	return eu_escape(eg, frame->str);
}
