#include <euphemus.h>
#include "euphemus_int.h"

struct initial_gen_frame {
	struct eu_stack_frame base;
	struct eu_value value;
};

static enum eu_result initial_gen_resume(struct eu_stack_frame *gframe,
					 void *v_eg)
{
	struct initial_gen_frame *frame = (struct initial_gen_frame *)gframe;
	struct eu_generate *eg = v_eg;

	if (eg->output != eg->output_end)
		return frame->value.metadata->generate(frame->value.metadata,
						       eg, frame->value.value);
	else
		return EU_REINSTATE_PAUSED;
}

struct eu_generate *eu_generate_create(struct eu_value value)
{
	struct eu_generate *eg = malloc(sizeof *eg);
	struct initial_gen_frame *frame;

	if (!eg)
		goto error;

	frame = eu_stack_init(&eg->stack, sizeof *frame);
	if (!frame)
		goto free_eg;

	frame->base.resume = initial_gen_resume;
	frame->base.destroy = eu_stack_frame_noop_destroy;
	frame->value = value;

	eg->error = 0;
	return eg;

 free_eg:
	free(eg);
 error:
	return NULL;
}

void eu_generate_destroy(struct eu_generate *eg)
{
	eu_stack_fini(&eg->stack);
	free(eg);
}

size_t eu_generate(struct eu_generate *eg, char *output, size_t len)
{
	if (unlikely(eg->error))
		return 0;

	eg->output = output;
	eg->output_end = output + len;

	switch (eu_stack_run(&eg->stack, eg)) {
	case EU_ERROR:
		eg->error = 1;

		/* fall through */
	default:
		return eg->output - output;
	}
}

int eu_generate_ok(struct eu_generate *eg)
{
	return !eg->error;
}

struct fixed_gen_frame {
	struct eu_stack_frame base;
	const char *str;
	unsigned int len;
};

static enum eu_result fixed_gen_resume(struct eu_stack_frame *gframe, void *eg)
{
	struct fixed_gen_frame *frame = (struct fixed_gen_frame *)gframe;
	return eu_fixed_gen_slow(eg, frame->str, frame->len);
}

enum eu_result eu_fixed_gen_slow(struct eu_generate *eg, const char *str,
				 unsigned int len)
{
	size_t space = eg->output_end - eg->output;
	struct fixed_gen_frame *frame;

	if (space >= len) {
		memcpy(eg->output, str, len);
		eg->output += len;
		return EU_OK;
	}

	memcpy(eg->output, str, space);
	eg->output += space;

	frame = eu_stack_alloc_first(&eg->stack, sizeof *frame);
	if (frame) {
		frame->base.resume = fixed_gen_resume;
		frame->base.destroy = eu_stack_frame_noop_destroy;
		frame->str = str + space;
		frame->len = len - space;
		return EU_PAUSED;
	}

	return EU_ERROR;
}

