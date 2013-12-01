#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "euphemus.h"
#include "euphemus_int.h"

static enum eu_parse_result initial_parse_resume(struct eu_parse *ep,
						 struct eu_parse_cont *cont)
{
	if (ep->input != ep->input_end) {
		return ep->metadata->parse(ep->metadata, ep, ep->result);
	}
	else {
		assert(!ep->outer_stack);
		ep->outer_stack = cont;
		return EU_PARSE_PAUSED;
	}
}

static void initial_parse_destroy(struct eu_parse *ep,
				  struct eu_parse_cont *cont)
{
	(void)cont;

	/* No parsing occured, so don't finalize the result. */
	ep->result = NULL;
}

struct eu_parse_cont initial_parse_cont = {
	NULL,
	initial_parse_resume,
	initial_parse_destroy
};

void eu_parse_init(struct eu_parse *ep, struct eu_metadata *metadata,
		   void *result)
{
	ep->outer_stack = &initial_parse_cont;
	ep->stack_top = ep->stack_bottom = NULL;
	ep->metadata = metadata;
	ep->result = result;
	ep->buf = NULL;
	ep->buf_size = 0;
	ep->error = 0;

	memset(result, 0, metadata->size);
}

void eu_parse_fini(struct eu_parse *ep)
{
	struct eu_parse_cont *c, *next;

	/* If the parse was unfinished, there might be stack frames to
	   clean up. */
	for (c = ep->outer_stack; c; c = next) {
		next = c->next;
		c->destroy(ep, c);
	}

	for (c = ep->stack_top; c; c = next) {
		next = c->next;
		c->destroy(ep, c);
	}

	/* Clean up the result, if it wasn't claimed via
	   eu_parse_finish. */
	if (ep->result)
		ep->metadata->fini(ep->metadata, ep->result);

	free(ep->buf);
}

void eu_parse_insert_cont(struct eu_parse *ep, struct eu_parse_cont *c)
{
	c->next = NULL;
	if (ep->stack_bottom) {
		ep->stack_bottom->next = c;
		ep->stack_bottom = c;
	}
	else {
		ep->stack_top = ep->stack_bottom = c;
	}
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

int eu_parse(struct eu_parse *ep, const char *input, size_t len)
{
	enum eu_parse_result res;

	if (ep->error)
		return 0;

	/* Need to concatenate the inner and outer stacks */
	if (ep->stack_top) {
		ep->stack_bottom->next = ep->outer_stack;
		ep->outer_stack = ep->stack_top;
		ep->stack_top = ep->stack_bottom = NULL;
	}

	ep->input = input;
	ep->input_end = input + len;

	for (;;) {
		struct eu_parse_cont *s = ep->outer_stack;
		if (!s)
			break;

		ep->outer_stack = s->next;
		res = s->resume(ep, s);
		if (res == EU_PARSE_OK)
			continue;

		if (res == EU_PARSE_PAUSED)
			return 1;
		else
			goto error;

	}

	ep->input = skip_whitespace(ep->input, ep->input_end);
	if (ep->input == ep->input_end)
		return 1;

 error:
	ep->error = 1;
	return 0;
}

int eu_parse_finish(struct eu_parse *ep)
{
	if (ep->error || ep->outer_stack || ep->stack_top)
		return 0;

	/* The client now has responsiblity for the result */
	ep->result = NULL;
	return 1;
}

struct consume_ws_cont {
	struct eu_parse_cont base;
	struct eu_metadata *metadata;
	void *result;
};

static enum eu_parse_result consume_ws_resume(struct eu_parse *ep,
					      struct eu_parse_cont *gcont);
static void consume_ws_destroy(struct eu_parse *ep,
			       struct eu_parse_cont *cont);

enum eu_parse_result eu_insert_whitespace_cont(struct eu_parse *ep,
					       struct eu_metadata *metadata,
					       void *result)
{
	struct consume_ws_cont *cont = malloc(sizeof *cont);
	if (!cont)
		return EU_PARSE_ERROR;

	cont->base.resume = consume_ws_resume;
	cont->base.destroy = consume_ws_destroy;
	cont->metadata = metadata;
	cont->result = result;
	eu_parse_insert_cont(ep, &cont->base);
	return EU_PARSE_PAUSED;
}

static enum eu_parse_result consume_ws_resume(struct eu_parse *ep,
					      struct eu_parse_cont *gcont)
{
	struct consume_ws_cont *cont = (struct consume_ws_cont *)gcont;
	const char *p = ep->input;
	const char *end = ep->input_end;

	ep->input = p = skip_whitespace(p, end);
	if (p != end) {
		struct eu_metadata *metadata = cont->metadata;
		void *result = cont->result;
		free(cont);
		return metadata->parse(metadata, ep, result);
	}

	eu_parse_insert_cont(ep, &cont->base);
	return EU_PARSE_PAUSED;
}

static void consume_ws_destroy(struct eu_parse *ep,
			       struct eu_parse_cont *cont)
{
	(void)ep;
	free(cont);
}
