#include <stdlib.h>

#include <euphemus.h>
#include "euphemus_int.h"

struct initial_parse_frame {
	struct eu_stack_frame base;
	struct eu_parse *ep;
};

static enum eu_parse_result initial_parse_resume(struct eu_stack_frame *frame,
						 void *v_ep)
{
	struct eu_parse *ep = v_ep;
	(void)frame;

	if (ep->input != ep->input_end)
		return ep->metadata->parse(ep->metadata, ep, ep->result);
	else
		return EU_PARSE_REINSTATE_PAUSED;
}

static void initial_parse_destroy(struct eu_stack_frame *gframe)
{
	struct initial_parse_frame *frame = (struct initial_parse_frame *)gframe;

	/* No parsing occured, so don't finalize the result. */
	frame->ep->result = NULL;
}

struct eu_parse *eu_parse_create(struct eu_value result)
{
	struct eu_parse *ep = malloc(sizeof *ep);
	struct initial_parse_frame *frame;

	if (!ep)
		goto error;

	/* Make the stack area just big enough for the
	   initial_parse_resume frame. Having some slack doesn't seem
	   to save much work. */
	frame = eu_stack_init(&ep->stack, sizeof(struct initial_parse_frame));
	if (!frame)
		goto free_ep;

	frame->base.resume = initial_parse_resume;
	frame->base.destroy = initial_parse_destroy;
	frame->ep = ep;

	ep->metadata = result.metadata;
	ep->result = result.value;
	ep->error = 0;

	memset(ep->result, 0, ep->metadata->size);
	return ep;

 free_ep:
	free(ep);
 error:
	return NULL;
}

void eu_parse_destroy(struct eu_parse *ep)
{
	eu_stack_fini(&ep->stack);

	/* Clean up the result, if it wasn't claimed via
	   eu_parse_finish. */
	if (ep->result)
		ep->metadata->fini(ep->metadata, ep->result);

	free(ep);
}

int eu_parse(struct eu_parse *ep, const char *input, size_t len)
{
	if (ep->error)
		return 0;

	ep->input = input;
	ep->input_end = input + len;

	if (eu_stack_run(&ep->stack, ep)) {
		/* Done parsing.  Check for trailing input. */
		ep->input = skip_whitespace(ep->input, ep->input_end);
		if (ep->input == ep->input_end)
			return 1;
	}

	ep->error = 1;
	return 0;
}

int eu_parse_finish(struct eu_parse *ep)
{
	if (ep->error || !eu_stack_empty(&ep->stack))
		return 0;

	/* The client now has responsiblity for the result */
	ep->result = NULL;
	return 1;
}

struct consume_ws_frame {
	struct eu_stack_frame base;
	const struct eu_metadata *metadata;
	void *result;
};

static enum eu_parse_result consume_ws_resume(struct eu_stack_frame *gframe,
					      void *v_ep);

enum eu_parse_result eu_consume_whitespace_pause(
					const struct eu_metadata *metadata,
					struct eu_parse *ep,
					void *result)
{
	struct consume_ws_frame *frame
		= eu_stack_alloc_first(&ep->stack, sizeof *frame);
	if (!frame)
		return EU_PARSE_ERROR;

	frame->base.resume = consume_ws_resume;
	frame->base.destroy = eu_stack_frame_noop_destroy;
	frame->metadata = metadata;
	frame->result = result;
	return EU_PARSE_PAUSED;
}

enum eu_parse_result eu_consume_ws_until_slow(const struct eu_metadata *metadata,
					      struct eu_parse *ep,
					      void *result,
					      char c)
{
	enum eu_parse_result res
		= eu_consume_whitespace(metadata, ep, result);
	if (res == EU_PARSE_OK && unlikely(*ep->input != c))
		res = EU_PARSE_ERROR;

	return res;
}

static enum eu_parse_result consume_ws_resume(struct eu_stack_frame *gframe,
					      void *v_ep)
{
	struct consume_ws_frame *frame = (struct consume_ws_frame *)gframe;
	struct eu_parse *ep = v_ep;
	const char *p = ep->input;
	const char *end = ep->input_end;

	ep->input = p = skip_whitespace(p, end);
	if (p != end)
		return frame->metadata->parse(frame->metadata, ep,
					      frame->result);
	else
		return eu_consume_whitespace_pause(frame->metadata, ep,
						   frame->result);
}

struct expect_parse_frame {
	struct eu_stack_frame base;
	const char *expect;
	unsigned int expect_len;
};

static enum eu_parse_result expect_parse_resume(struct eu_stack_frame *gframe,
						void *v_ep);

enum eu_parse_result eu_parse_expect_slow(struct eu_parse *ep,
					  const char *expect,
					  unsigned int expect_len)
{
	size_t avail = ep->input_end - ep->input;
	struct expect_parse_frame *frame;

	if (expect_len <= avail) {
		if (!memcmp(ep->input, expect, expect_len)) {
			ep->input += expect_len;
			return EU_PARSE_OK;
		}

		return EU_PARSE_ERROR;
	}

	if (memcmp(ep->input, expect, avail))
		return EU_PARSE_ERROR;

	ep->input += avail;

	frame = eu_stack_alloc_first(&ep->stack, sizeof *frame);
	if (frame) {
		frame->base.resume = expect_parse_resume;
		frame->base.destroy = eu_stack_frame_noop_destroy;
		frame->expect = expect + avail;
		frame->expect_len = expect_len - avail;
		return EU_PARSE_PAUSED;
	}

	return EU_PARSE_ERROR;
}

static enum eu_parse_result expect_parse_resume(struct eu_stack_frame *gframe,
						void *v_ep)
{
	struct expect_parse_frame *frame
		= (struct expect_parse_frame *)gframe;
	return eu_parse_expect_slow(v_ep, frame->expect, frame->expect_len);
}
