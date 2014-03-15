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

	/* Make the stack area just big enough for the
	   initial_parse_resume frame. Having some slack doesn't seem
	   to save much work. */
	ep->stack_area_size = sizeof(struct eu_parse_cont);
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

	ep->scratch_size = ep->new_stack_top = ep->new_stack_bottom = 0;
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
	free(ep);
}

STATIC_ASSERT((sizeof(void *) & (sizeof(void *) - 1)) == 0);

/* Round up to a multiple of sizeof(void *).  This ought to be
 * sufficient to suitably align all stack frames. */
#define ROUND_UP(n) ((((n) - 1) & -sizeof(void *)) + sizeof(void *))

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

	ep->new_stack_bottom = ep->new_stack_top = ROUND_UP(ep->scratch_size);
}

void *eu_parse_alloc_cont(struct eu_parse *ep, size_t size)
{
	size_t new_stack_top;
	struct eu_parse_cont *f;

	size = ROUND_UP(size);
	new_stack_top = ep->new_stack_top + size;

	/* Do we have space for the new stack frame */
	if (unlikely(new_stack_top > ep->old_stack_bottom)) {
		/* Need to expand the stack area, creating a bigger
		   gap between the new and old stack regions. */
		size_t old_stack_size
			= ep->stack_area_size - ep->old_stack_bottom;
		char *stack;

		do {
			ep->stack_area_size *= 2;
		} while (ep->stack_area_size < new_stack_top + old_stack_size);

		stack = malloc(ep->stack_area_size);
		if (stack == NULL)
			return NULL;

		memcpy(stack, ep->stack, ep->new_stack_top);
		memcpy(stack + ep->stack_area_size - old_stack_size,
		       ep->stack + ep->old_stack_bottom, old_stack_size);
		free(ep->stack);

		ep->stack = stack;
		ep->old_stack_bottom = ep->stack_area_size - old_stack_size;
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

int eu_parse_reserve_scratch(struct eu_parse *ep, size_t s)
{
	size_t new_stack_size, old_stack_size, min_size;
	char *stack;

	if (ep->new_stack_bottom != ep->new_stack_top) {
		/* There is a new stack region */
		if (s <= ep->new_stack_bottom)
			return 1;

		/* Can we make space by consolidating the new stack
		   with the old_stack? */
		new_stack_size = ep->new_stack_top - ep->new_stack_bottom;
		if (s <= ep->old_stack_bottom - new_stack_size) {
			eu_parse_begin_pause(ep);
			return 1;
		}
	}
	else {
		if (s <= ep->old_stack_bottom)
			return 1;

		new_stack_size = 0;
	}

	/* Need to grow the stack area */
	old_stack_size = ep->stack_area_size - ep->old_stack_bottom;
	min_size = ROUND_UP(s) + new_stack_size + old_stack_size;

	do {
		ep->stack_area_size *= 2;
	} while (ep->stack_area_size < min_size);

	stack = malloc(ep->stack_area_size);
	if (stack == NULL)
		return 0;

	memcpy(stack, ep->stack, ep->scratch_size);

	memcpy(stack + ep->stack_area_size - old_stack_size,
	       ep->stack + ep->old_stack_bottom,
	       old_stack_size);
	ep->old_stack_bottom = ep->stack_area_size - old_stack_size;

	memcpy(stack + ep->old_stack_bottom - new_stack_size,
	       ep->stack + ep->new_stack_bottom, new_stack_size);
	ep->new_stack_top = ep->old_stack_bottom;
	ep->new_stack_bottom = ep->new_stack_top - new_stack_size;

	free(ep->stack);
	ep->stack = stack;
	return 1;
}

void eu_parse_reset_scratch(struct eu_parse *ep)
{
	ep->scratch_size = 0;
}

int eu_parse_copy_to_scratch(struct eu_parse *ep, const char *start,
			     const char *end)
{
	size_t len = end - start;

	if (eu_parse_reserve_scratch(ep, len)) {
		memcpy(ep->stack, start, len);
		ep->scratch_size = len;
		return 1;
	}
	else {
		return 0;
	}
}

int eu_parse_append_to_scratch(struct eu_parse *ep, const char *start,
			       const char *end)
{
	size_t len = end - start;

	if (eu_parse_reserve_scratch(ep, ep->scratch_size + len)) {
		memcpy(ep->stack + ep->scratch_size, start, len);
		ep->scratch_size += len;
		return 1;
	}
	else {
		return 0;
	}
}

int eu_parse_append_to_scratch_with_nul(struct eu_parse *ep, const char *start,
					const char *end)
{
	size_t len = end - start;

	if (eu_parse_reserve_scratch(ep, ep->scratch_size + len + 1)) {
		memcpy(ep->stack + ep->scratch_size, start, len);
		ep->scratch_size += len;
		ep->stack[ep->scratch_size++] = 0;
		return 1;
	}
	else {
		return 0;
	}
}

void eu_parse_cont_noop_destroy(struct eu_parse *ep, struct eu_parse_cont *cont)
{
	(void)ep;
	(void)cont;
}

struct consume_ws_cont {
	struct eu_parse_cont base;
	const struct eu_metadata *metadata;
	void *result;
};

static enum eu_parse_result consume_ws_resume(struct eu_parse *ep,
					      struct eu_parse_cont *gcont);

enum eu_parse_result eu_consume_whitespace_pause(
					const struct eu_metadata *metadata,
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
