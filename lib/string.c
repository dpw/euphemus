#include <euphemus.h>

#include "euphemus_int.h"
#include "unescape.h"

static eu_bool_t assign_trimming(struct eu_string *result, char *buf,
				 size_t len, size_t capacity)
{
	if (capacity - len > capacity / 4) {
		buf = realloc(buf, len);
		if (unlikely(!buf))
			return 0;
	}

	result->chars = buf;
	result->len = len;
	return 1;
}

struct string_parse_frame {
	struct eu_stack_frame base;
	struct eu_string *result;
	char *buf;
	size_t len;
	size_t capacity;
	eu_unescape_state_t unescape;
};

static enum eu_result string_parse_resume(struct eu_stack_frame *gframe,
					  void *v_ep);
static void string_parse_frame_destroy(struct eu_stack_frame *gframe);

static struct string_parse_frame *alloc_frame(struct eu_parse *ep, const char *p,
					    struct eu_string *result)
{
	struct string_parse_frame *frame
		= eu_stack_alloc_first(&ep->stack, sizeof *frame);

	if (frame) {
		frame->base.resume = string_parse_resume;
		frame->base.destroy = string_parse_frame_destroy;
		frame->result = result;
		frame->len = p - ep->input;
		frame->capacity = frame->len * 2;
		frame->unescape = 0;
		frame->buf = malloc(frame->capacity);
		if (frame->buf)
			return frame;
	}

	return NULL;
}

static enum eu_result string_parse_common(const struct eu_metadata *metadata,
					  struct eu_parse *ep, void *v_result)
{
	const char *p = ep->input;
	const char *end = ep->input_end;
	struct eu_string *result = v_result;
	struct string_parse_frame *frame;
	char *buf;
	size_t len;

	(void)metadata;

	ep->input = ++p;

	for (;; p++) {
		if (p == end)
			goto pause;

		switch (*p) {
		case '\"': goto done;
		case '\\': goto unescape;
		default: break;
		}
	}

 done:
	len = p - ep->input;
	if (!len)
		goto empty;

	buf = malloc(len);
	if (!buf)
		goto alloc_error;

	memcpy(buf, ep->input, len);
	result->chars = buf;
	result->len = len;

	/* skip the final '"' */
	ep->input = p + 1;
	return EU_OK;

 empty:
	result->chars = EU_ZERO_LENGTH_PTR;

	ep->input = p + 1;
	return EU_OK;

 pause:
	frame = alloc_frame(ep, p, result);
	if (!frame)
		goto alloc_error;

	memcpy(frame->buf, ep->input, frame->len);
	ep->input = p;
	return EU_PAUSED;

 unescape:
	/* Skip the backslash, and scan forward to find the end of the
	   string */
	do {
		if (++p == end)
			goto pause_unescape;
	} while (*p != '\"' || quotes_escaped(p));

	len = p - ep->input;
	buf = malloc(len);
	if (!buf)
		goto alloc_error;

	{
		eu_unescape_state_t ues;
		end = eu_unescape(ep, p, buf, &ues);
		if (!end || ues)
			goto error_free_buf;
	}

	if (unlikely(!assign_trimming(result, buf, end - buf, len)))
		goto error_free_buf;

	/* skip the final '"' */
	ep->input = p + 1;
	return EU_OK;

 pause_unescape:
	frame = alloc_frame(ep, p, result);
	if (!frame)
		goto alloc_error;

	end = eu_unescape(ep, p, frame->buf, &frame->unescape);
	if (!end)
		goto error;

	frame->len = end - frame->buf;
	ep->input = p;
	return EU_PAUSED;

 alloc_error:
	return EU_ERROR;

 error_free_buf:
	free(buf);
 error:
	return EU_ERROR;
}

static enum eu_result string_parse_resume(struct eu_stack_frame *gframe,
					  void *v_ep)
{
	struct string_parse_frame *frame = (struct string_parse_frame *)gframe;
	struct eu_parse *ep = v_ep;
	const char *p, *end;
	char *buf;
	size_t len, total_len;
	int unescaped_char_len = 0;
	eu_unicode_char_t unescaped_char;

	if (unlikely(frame->unescape)) {
		if (!eu_finish_unescape(ep, &frame->unescape, &unescaped_char))
			goto error;

		if (frame->unescape)
			return EU_REINSTATE_PAUSED;

		unescaped_char_len = eu_unicode_utf8_length(unescaped_char);
	}

	p = ep->input;
	end = ep->input_end;

	for (;; p++) {
		if (p == end)
			goto pause;

		switch (*p) {
		case '\"': goto done;
		case '\\': goto unescape;
		default: break;
		}
	}

 done:
	len = p - ep->input;
	total_len = frame->len + len + unescaped_char_len;
	buf = frame->buf;
	if (!total_len)
		goto empty;

	if (total_len > frame->capacity) {
		buf = realloc(buf, total_len);
		if (!buf)
			goto alloc_error;

		frame->buf = buf;
		frame->capacity = total_len;
	}

	if (unlikely(unescaped_char_len)) {
		eu_unicode_to_utf8(unescaped_char, buf + frame->len);
		frame->len += unescaped_char_len;
	}

	memcpy(buf + frame->len, ep->input, len);

	if (unlikely(!assign_trimming(frame->result, buf, total_len,
				      frame->capacity)))
		goto alloc_error;

	/* skip the final '"' */
	ep->input = p + 1;
	return EU_OK;

 empty:
	free(buf);
	frame->result->chars = EU_ZERO_LENGTH_PTR;
	ep->input = p + 1;
	return EU_OK;

 pause:
	len = p - ep->input;
	total_len = frame->len + len + unescaped_char_len;

	buf = frame->buf;
	if (total_len > frame->capacity) {
		size_t new_capacity = total_len * 2;
		buf = realloc(buf, new_capacity);
		if (!buf)
			goto alloc_error;

		frame->buf = buf;
		frame->capacity = new_capacity;
	}

	if (unlikely(unescaped_char_len)) {
		eu_unicode_to_utf8(unescaped_char, buf + frame->len);
		frame->len += unescaped_char_len;
	}

	memcpy(buf + frame->len, ep->input, len);
	frame->len = total_len;
	ep->input = p;
	return EU_REINSTATE_PAUSED;

 unescape:
	/* Skip the backslash, and scan forward to find the end of the
	   string */
	do {
		if (++p == end)
			goto pause_unescape;
	} while (*p != '\"' || quotes_escaped_bounded(p, ep->input));

	len = p - ep->input;
	total_len = frame->len + len + unescaped_char_len;

	buf = frame->buf;
	if (total_len > frame->capacity) {
		frame->capacity = total_len;
		buf = realloc(buf, total_len);
		if (!buf)
			goto alloc_error;

		frame->buf = buf;
	}

	if (unlikely(unescaped_char_len)) {
		eu_unicode_to_utf8(unescaped_char, buf + frame->len);
		frame->len += unescaped_char_len;
	}

	{
		eu_unescape_state_t ues;
		end = eu_unescape(ep, p, buf + frame->len, &ues);
		if (!end || ues)
			goto error;
	}

	if (unlikely(!assign_trimming(frame->result, buf, end - buf,
				      frame->capacity)))
		goto alloc_error;

	ep->input = p + 1;
	return EU_OK;

 pause_unescape:
	len = p - ep->input;
	total_len = frame->len + len + unescaped_char_len;

	buf = frame->buf;
	if (total_len > frame->capacity) {
		size_t new_capacity = total_len * 2;
		buf = realloc(buf, new_capacity);
		if (!buf)
			goto alloc_error;

		frame->buf = buf;
		frame->capacity = new_capacity;
	}

	if (unlikely(unescaped_char_len)) {
		eu_unicode_to_utf8(unescaped_char, buf + frame->len);
		frame->len += unescaped_char_len;
	}

	end = eu_unescape(ep, p, buf + frame->len, &frame->unescape);
	frame->len = end - buf;
	ep->input = p;
	return EU_REINSTATE_PAUSED;

 alloc_error:
 error:
	free(frame->buf);
	return EU_ERROR;
}

static void string_parse_frame_destroy(struct eu_stack_frame *gframe)
{
	struct string_parse_frame *frame = (struct string_parse_frame *)gframe;
	free(frame->buf);
}

enum eu_result eu_variant_string(const void *string_metadata,
				 struct eu_parse *ep, struct eu_variant *result)
{
	result->metadata = string_metadata;
	return string_parse_common(string_metadata, ep, &result->u.string);
}

static enum eu_result string_parse(const struct eu_metadata *metadata,
				   struct eu_parse *ep, void *result)
{
	enum eu_result res
		= eu_consume_whitespace_until(metadata, ep, result, '\"');

	if (res == EU_OK)
		return string_parse_common(metadata, ep, result);
	else
		return res;
}

struct string_gen_frame {
	struct eu_stack_frame base;
	struct eu_string str;
};

static enum eu_result string_gen_resume(struct eu_stack_frame *gframe,
					void *eg);

static enum eu_result string_generate(const struct eu_metadata *metadata,
				      struct eu_generate *eg, void *value)
{
	struct eu_string *str = value;
	struct string_gen_frame *frame;
	size_t space;

	/* metadata is NULL when resuming */
	if (metadata)
		/* We always get called with at least a byte of space. */
		*eg->output++ = '\"';

	space = eg->output_end - eg->output;
	if (str->len < space) {
		/* TODO escaping */

		memcpy(eg->output, str->chars, str->len);
		eg->output += str->len;
		*eg->output++ = '\"';
		return EU_OK;
	}

	memcpy(eg->output, str->chars, space);
	eg->output += space;

	frame = eu_stack_alloc_first(&eg->stack, sizeof *frame);
	if (frame) {
		frame->base.resume = string_gen_resume;
		frame->base.destroy = eu_stack_frame_noop_destroy;
		frame->str.chars = str->chars + space;
		frame->str.len = str->len - space;
		return EU_PAUSED;
	}

	return EU_ERROR;
}

static enum eu_result string_gen_resume(struct eu_stack_frame *gframe,
					void *eg)
{
	struct string_gen_frame *frame = (struct string_gen_frame *)gframe;
	return string_generate(NULL, eg, &frame->str);
}

static void string_fini(const struct eu_metadata *metadata, void *value)
{
	struct eu_string *str = value;
	(void)metadata;

	eu_string_fini(str);
}

const struct eu_metadata eu_string_metadata = {
	EU_JSON_STRING,
	sizeof(struct eu_string),
	string_parse,
	string_generate,
	string_fini,
	eu_get_fail,
	eu_object_iter_init_fail,
	eu_object_size_fail
};
