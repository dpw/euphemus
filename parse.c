#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "euphemus.h"
#include "euphemus_int.h"

void eu_parse_init(struct eu_parse *ep, struct eu_metadata *metadata,
		   void *result)
{
	ep->outer_stack = &metadata->base;
	ep->stack_top = ep->stack_bottom = NULL;
	ep->metadata = metadata;
	ep->result = result;
	ep->member_name_buf = NULL;
	ep->member_name_size = 0;
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
		ep->metadata->destroy(ep->metadata, ep->result);

	free(ep->member_name_buf);
}

static void set_only_cont(struct eu_parse *ep, struct eu_parse_cont *c)
{
	assert(!ep->outer_stack);
	ep->outer_stack = c;
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

int eu_parse_set_member_name(struct eu_parse *ep, const char *start,
			     const char *end)
{
	size_t len = end - start;

	if (len > ep->member_name_size) {
		free(ep->member_name_buf);
		if (!(ep->member_name_buf = malloc(len)))
			return 0;

		ep->member_name_size = len;
	}

	memcpy(ep->member_name_buf, start, len);
	ep->member_name_len = len;
	return 1;
}

int eu_parse_append_member_name(struct eu_parse *ep, const char *start,
				const char *end)
{
	size_t len = end - start;
	size_t total_len = ep->member_name_len + len;

	if (total_len > ep->member_name_size) {
		char *buf = malloc(total_len);
		if (!buf)
			return 0;

		ep->member_name_size = total_len;
		memcpy(buf, ep->member_name_buf, ep->member_name_len);
		free(ep->member_name_buf);
		ep->member_name_buf = buf;
	}

	memcpy(ep->member_name_buf + ep->member_name_len, start, len);
	ep->member_name_len = total_len;
	return 1;
}

enum eu_parse_result eu_parse_metadata_cont_resume(struct eu_parse *ep,
						   struct eu_parse_cont *cont)
{
	struct eu_metadata *metadata = (struct eu_metadata *)cont;

	ep->input = skip_whitespace(ep->input, ep->input_end);

	if (ep->input != ep->input_end) {
		return metadata->parse(metadata, ep, ep->result);
	}
	else {
		set_only_cont(ep, cont);
		return EU_PARSE_PAUSED;
	}
}

void eu_parse_metadata_cont_destroy(struct eu_parse *ep,
				    struct eu_parse_cont *cont)
{
	(void)cont;

	/* No parsing occured, so don't finalize the result. */
	ep->result = NULL;
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
