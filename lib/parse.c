#include <stdlib.h>
#include <assert.h>

#include <euphemus.h>
#include "euphemus_int.h"

static void initial_parse_destroy(struct eu_parse *ep,
				  struct eu_parse_cont *cont);

static enum eu_parse_result initial_parse_resume(struct eu_parse *ep,
						 struct eu_parse_cont *cont)
{
	(void)cont;

	if (ep->input != ep->input_end)
		return ep->metadata->parse(ep->metadata, ep, ep->result);
	else
		return EU_PARSE_REINSTATE_PAUSED;
}

static void initial_parse_destroy(struct eu_parse *ep,
				  struct eu_parse_cont *cont)
{
	(void)cont;

	/* No parsing occured, so don't finalize the result. */
	ep->result = NULL;
}

struct eu_parse *eu_parse_create(struct eu_value result)
{
	struct eu_parse *ep = malloc(sizeof *ep);
	char *stack;
	struct eu_parse_cont *cont;

	if (!ep)
		goto error;

	ep->stack_area_size = 128;
	stack = malloc(ep->stack_area_size);
	if (!stack)
		goto free_ep;

	ep->stack = stack;
	ep->old_stack_bottom
		= ep->stack_area_size - sizeof(struct eu_parse_cont);

	cont = (struct eu_parse_cont *)(stack + ep->old_stack_bottom);
	cont->size = sizeof(struct eu_parse_cont);
	cont->resume = initial_parse_resume;
	cont->destroy = initial_parse_destroy;

	ep->new_stack_top = ep->new_stack_bottom = 0;
	ep->metadata = result.metadata;
	ep->result = result.value;
	ep->buf = NULL;
	ep->buf_size = 0;
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
	struct eu_parse_cont *c;

	/* If the parse was unfinished, there might be stack frames to
	   clean up. */
	while (ep->new_stack_bottom != ep->new_stack_top) {
		c = (struct eu_parse_cont *)(ep->stack + ep->new_stack_bottom);
		ep->new_stack_bottom += c->size;
		c->destroy(ep, c);
	}

	while (ep->old_stack_bottom != ep->stack_area_size) {
		c = (struct eu_parse_cont *)(ep->stack + ep->old_stack_bottom);
		ep->old_stack_bottom += c->size;
		c->destroy(ep, c);
	}

	/* Clean up the result, if it wasn't claimed via
	   eu_parse_finish. */
	if (ep->result)
		ep->metadata->fini(ep->metadata, ep->result);

	free(ep->stack);
	free(ep->buf);
	free(ep);
}

int eu_parse_set_buffer(struct eu_parse *ep, const char *start, const char *end)
{
	size_t len = end - start;

	if (len > ep->buf_size) {
		free(ep->buf);
		if (!(ep->buf = malloc(len)))
			return 0;

		ep->buf_size = len;
	}

	memcpy(ep->buf, start, len);
	ep->buf_len = len;
	return 1;
}

int eu_parse_append_buffer(struct eu_parse *ep, const char *start,
			   const char *end)
{
	size_t len = end - start;
	size_t total_len = ep->buf_len + len + 1;

	if (total_len > ep->buf_size) {
		char *buf = malloc(total_len);
		if (!buf)
			return 0;

		ep->buf_size = total_len;
		memcpy(buf, ep->buf, ep->buf_len);
		free(ep->buf);
		ep->buf = buf;
	}

	memcpy(ep->buf + ep->buf_len, start, len);
	ep->buf_len = total_len - 1;
	return 1;
}

/* Like eu_parse_append_buffer, but ensure the buffer is NUL terminated. */
int eu_parse_append_buffer_nul(struct eu_parse *ep, const char *start,
			       const char *end)
{
	if (!eu_parse_append_buffer(ep, start, end))
		return 0;

	ep->buf[ep->buf_len] = 0;
	return 1;
}

void eu_parse_begin_pause(struct eu_parse *ep)
{
	if (ep->new_stack_top != ep->new_stack_bottom) {
		/* There is a new stack from a previous pause.
		   Consolidate it with the old stack. */
		size_t new_stack_size
			= ep->new_stack_top - ep->new_stack_bottom;
		ep->old_stack_bottom -= new_stack_size;
		memmove(ep->stack + ep->old_stack_bottom,
			ep->stack + ep->new_stack_bottom,
			new_stack_size);
	}

	ep->new_stack_bottom = ep->new_stack_top = 0;
}

void *eu_parse_alloc_cont(struct eu_parse *ep, size_t size)
{
	size_t new_stack_top;
	struct eu_parse_cont *f;

	/* Round size up to the pointer width; this should be
	   sufficient to suitably align all stack frames. */
	size = ((size - 1) & -sizeof(void *)) + sizeof(void *);

	new_stack_top = ep->new_stack_top + size;

	/* Do we have space for the new stack frame */
	if (unlikely(new_stack_top > ep->old_stack_bottom)) {
		/* Need to expand the stack area, creating a bigger
		   gap between the new and old stack regions. */
		size_t stack_area_size = ep->stack_area_size;
		size_t old_stack_size = stack_area_size - ep->old_stack_bottom;
		char *s;

		do {
			stack_area_size *= 2;
		} while (stack_area_size - ep->stack_area_size
				< new_stack_top - ep->old_stack_bottom);

		s = malloc(stack_area_size);
		if (s == NULL)
			return NULL;

		memcpy(s, ep->stack, ep->new_stack_top);
		memcpy(s + stack_area_size - old_stack_size,
		       ep->stack + ep->old_stack_bottom, old_stack_size);
		free(ep->stack);

		ep->stack = s;
		ep->old_stack_bottom = stack_area_size - old_stack_size;
		ep->stack_area_size = stack_area_size;
	}

	f = (struct eu_parse_cont *)(ep->stack + ep->new_stack_top);
	f->size = size;
	ep->new_stack_top = new_stack_top;
	return f;
}

void *eu_parse_alloc_first_cont(struct eu_parse *ep, size_t size)
{
	eu_parse_begin_pause(ep);
	return eu_parse_alloc_cont(ep, size);
}

int eu_parse(struct eu_parse *ep, const char *input, size_t len)
{
	struct eu_parse_cont *c;

	if (ep->error)
		return 0;

	ep->input = input;
	ep->input_end = input + len;

	/* Process stack frames from the new stack */
	while (ep->new_stack_bottom != ep->new_stack_top) {
		c = (struct eu_parse_cont *)(ep->stack + ep->new_stack_bottom);
		ep->new_stack_bottom += c->size;

		switch (c->resume(ep, c)) {
		case EU_PARSE_OK:
			break;

		case EU_PARSE_REINSTATE_PAUSED:
			ep->new_stack_bottom -= c->size;
			/* fall through */

		case EU_PARSE_PAUSED:
			return 1;

		case EU_PARSE_ERROR:
			goto error;
		}
	}

	/* Process stack frames from the old stack */
	while (ep->old_stack_bottom != ep->stack_area_size) {
		c = (struct eu_parse_cont *)(ep->stack + ep->old_stack_bottom);
		ep->old_stack_bottom += c->size;

		switch (c->resume(ep, c)) {
		case EU_PARSE_OK:
			break;

		case EU_PARSE_REINSTATE_PAUSED:
			ep->old_stack_bottom -= c->size;
			/* fall through */

		case EU_PARSE_PAUSED:
			return 1;

		case EU_PARSE_ERROR:
			goto error;
		}
	}

	/* Done parsing.  Check for trailing input. */
	ep->input = skip_whitespace(ep->input, ep->input_end);
	if (ep->input == ep->input_end)
		return 1;

 error:
	ep->error = 1;
	return 0;
}

int eu_parse_finish(struct eu_parse *ep)
{
	if (ep->error
	    || ep->new_stack_bottom != ep->new_stack_top
	    || ep->old_stack_bottom != ep->stack_area_size)
		return 0;

	/* The client now has responsiblity for the result */
	ep->result = NULL;
	return 1;
}

void eu_parse_cont_noop_destroy(struct eu_parse *ep, struct eu_parse_cont *cont)
{
	(void)ep;
	(void)cont;
}

struct consume_ws_cont {
	struct eu_parse_cont base;
	struct eu_metadata *metadata;
	void *result;
};

static enum eu_parse_result consume_ws_resume(struct eu_parse *ep,
					      struct eu_parse_cont *gcont);

enum eu_parse_result eu_consume_whitespace_pause(struct eu_metadata *metadata,
						 struct eu_parse *ep,
						 void *result)
{
	struct consume_ws_cont *cont
		= eu_parse_alloc_first_cont(ep, sizeof *cont);
	if (!cont)
		return EU_PARSE_ERROR;

	cont->base.resume = consume_ws_resume;
	cont->base.destroy = eu_parse_cont_noop_destroy;
	cont->metadata = metadata;
	cont->result = result;
	return EU_PARSE_PAUSED;
}

enum eu_parse_result eu_consume_ws_until_slow(struct eu_metadata *metadata,
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

static enum eu_parse_result consume_ws_resume(struct eu_parse *ep,
					      struct eu_parse_cont *gcont)
{
	struct consume_ws_cont *cont = (struct consume_ws_cont *)gcont;
	const char *p = ep->input;
	const char *end = ep->input_end;

	ep->input = p = skip_whitespace(p, end);
	if (p != end)
		return cont->metadata->parse(cont->metadata, ep, cont->result);
	else
		return eu_consume_whitespace_pause(cont->metadata, ep,
						   cont->result);
}

void eu_noop_fini(struct eu_metadata *metadata, void *value)
{
	(void)metadata;
	(void)value;
}

struct expect_parse_cont {
	struct eu_parse_cont base;
	const char *expect;
	unsigned int expect_len;
};

static enum eu_parse_result expect_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont);

enum eu_parse_result eu_parse_expect_slow(struct eu_parse *ep,
					  const char *expect,
					  unsigned int expect_len)
{
	size_t avail = ep->input_end - ep->input;
	struct expect_parse_cont *cont;

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

	cont = eu_parse_alloc_first_cont(ep, sizeof *cont);
	if (cont) {
		cont->base.resume = expect_parse_resume;
		cont->base.destroy = eu_parse_cont_noop_destroy;
		cont->expect = expect + avail;
		cont->expect_len = expect_len - avail;
		return EU_PARSE_PAUSED;
	}

	return EU_PARSE_ERROR;
}

static enum eu_parse_result expect_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont)
{
	struct expect_parse_cont *cont = (struct expect_parse_cont *)gcont;
	return eu_parse_expect_slow(ep, cont->expect, cont->expect_len);
}
