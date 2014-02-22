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

struct string_parse_cont {
	struct eu_parse_cont base;
	struct eu_string *result;
	char *buf;
	size_t len;
	size_t capacity;
	eu_unescape_state_t unescape;
};

static enum eu_parse_result string_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont);
static void string_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *cont);

static struct string_parse_cont *alloc_cont(struct eu_parse *ep, const char *p,
					    struct eu_string *result)
{
	struct string_parse_cont *cont
		= eu_parse_alloc_first_cont(ep, sizeof *cont);

	if (cont) {
		cont->base.resume = string_parse_resume;
		cont->base.destroy = string_parse_cont_destroy;
		cont->result = result;
		cont->len = p - ep->input;
		cont->capacity = cont->len * 2;
		cont->unescape = 0;
		cont->buf = malloc(cont->capacity);
		if (cont->buf)
			return cont;
	}

	return NULL;
}

static enum eu_parse_result string_parse_common(struct eu_metadata *metadata,
						struct eu_parse *ep,
						void *v_result)
{
	const char *p = ep->input;
	const char *end = ep->input_end;
	struct eu_string *result = v_result;
	struct string_parse_cont *cont;
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
	buf = malloc(len);
	if (!buf)
		goto alloc_error;

	memcpy(buf, ep->input, len);
	result->chars = buf;
	result->len = len;

	/* skip the final '"' */
	ep->input = p + 1;
	return EU_PARSE_OK;

 pause:
	cont = alloc_cont(ep, p, result);
	if (!cont)
		goto alloc_error;

	memcpy(cont->buf, ep->input, cont->len);
	ep->input = p;
	return EU_PARSE_PAUSED;

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
			return EU_PARSE_ERROR;
	}

	if (unlikely(!assign_trimming(result, buf, end - buf, len))) {
		free(buf);
		return EU_PARSE_ERROR;
	}

	/* skip the final '"' */
	ep->input = p + 1;
	return EU_PARSE_OK;

 pause_unescape:
	cont = alloc_cont(ep, p, result);
	if (!cont)
		goto alloc_error;

	end = eu_unescape(ep, p, cont->buf, &cont->unescape);
	if (!end)
		return EU_PARSE_ERROR;

	cont->len = end - cont->buf;
	ep->input = p;
	return EU_PARSE_PAUSED;

 alloc_error:
	ep->input = p;
	return EU_PARSE_ERROR;
}

static enum eu_parse_result string_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont)
{
	struct string_parse_cont *cont = (struct string_parse_cont *)gcont;
	const char *p, *end;
	char *buf;
	size_t len, total_len;
	int unescaped_char_len = 0;
	eu_unicode_char_t unescaped_char;

	if (unlikely(cont->unescape)) {
		if (!eu_finish_unescape(ep, &cont->unescape, &unescaped_char))
			return EU_PARSE_ERROR;

		if (cont->unescape)
			return EU_PARSE_REINSTATE_PAUSED;

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
	total_len = cont->len + len + unescaped_char_len;

	buf = cont->buf;
	if (total_len > cont->capacity) {
		buf = realloc(buf, total_len);
		if (!buf)
			goto alloc_error;

		cont->buf = buf;
		cont->capacity = total_len;
	}

	if (unlikely(unescaped_char_len)) {
		eu_unicode_to_utf8(unescaped_char, buf + cont->len);
		cont->len += unescaped_char_len;
	}

	memcpy(buf + cont->len, ep->input, len);

	if (unlikely(!assign_trimming(cont->result, buf, total_len,
				      cont->capacity)))
		goto alloc_error;

	ep->input = p + 1;
	return EU_PARSE_OK;

 pause:
	len = p - ep->input;
	total_len = cont->len + len + unescaped_char_len;

	buf = cont->buf;
	if (total_len > cont->capacity) {
		size_t new_capacity = total_len * 2;
		buf = realloc(buf, new_capacity);
		if (!buf)
			goto alloc_error;

		cont->buf = buf;
		cont->capacity = new_capacity;
	}

	if (unlikely(unescaped_char_len)) {
		eu_unicode_to_utf8(unescaped_char, buf + cont->len);
		cont->len += unescaped_char_len;
	}

	memcpy(buf + cont->len, ep->input, len);
	cont->len = total_len;
	ep->input = p;
	return EU_PARSE_REINSTATE_PAUSED;

 unescape:
	/* Skip the backslash, and scan forward to find the end of the
	   string */
	do {
		if (++p == end)
			goto pause_unescape;
	} while (*p != '\"' || quotes_escaped_bounded(p, ep->input));

	len = p - ep->input;
	total_len = cont->len + len + unescaped_char_len;

	buf = cont->buf;
	if (total_len > cont->capacity) {
		cont->capacity = total_len;
		buf = realloc(buf, total_len);
		if (!buf)
			goto alloc_error;
	}

	if (unlikely(unescaped_char_len)) {
		eu_unicode_to_utf8(unescaped_char, buf + cont->len);
		cont->len += unescaped_char_len;
	}

	{
		eu_unescape_state_t ues;
		end = eu_unescape(ep, p, buf + cont->len, &ues);
		if (!end || ues)
			return EU_PARSE_ERROR;
	}

	if (unlikely(!assign_trimming(cont->result, buf, end - buf,
				      cont->capacity)))
		goto alloc_error;

	ep->input = p + 1;
	return EU_PARSE_OK;

 pause_unescape:
	len = p - ep->input;
	total_len = cont->len + len + unescaped_char_len;

	buf = cont->buf;
	if (total_len > cont->capacity) {
		size_t new_capacity = total_len * 2;
		buf = realloc(buf, new_capacity);
		if (!buf)
			goto alloc_error;

		cont->buf = buf;
		cont->capacity = new_capacity;
	}

	if (unlikely(unescaped_char_len)) {
		eu_unicode_to_utf8(unescaped_char, buf + cont->len);
		cont->len += unescaped_char_len;
	}

	end = eu_unescape(ep, p, buf + cont->len, &cont->unescape);
	cont->len = end - buf;
	ep->input = p;
	return EU_PARSE_REINSTATE_PAUSED;

 alloc_error:
	free(cont->buf);
	return EU_PARSE_ERROR;
}

static void string_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *gcont)
{
	struct string_parse_cont *cont = (struct string_parse_cont *)gcont;

	(void)ep;

	free(cont->buf);
}

enum eu_parse_result eu_variant_string(void *string_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result)
{
	result->metadata = string_metadata;
	return string_parse_common(string_metadata, ep, &result->u.string);
}

static enum eu_parse_result string_parse(struct eu_metadata *metadata,
					 struct eu_parse *ep,
					 void *result)
{
	enum eu_parse_result res
		= eu_consume_whitespace_until(metadata, ep, result, '\"');

	if (res == EU_PARSE_OK)
		return string_parse_common(metadata, ep, result);
	else
		return res;
}

static void string_fini(struct eu_metadata *metadata, void *value)
{
	struct eu_string *str = value;
	(void)metadata;

	eu_string_fini(str);
}

struct eu_metadata eu_string_metadata = {
	EU_JSON_STRING,
	sizeof(struct eu_string),
	string_parse,
	string_fini,
	eu_get_fail,
	eu_object_iter_init_fail
};
