#include <limits.h>

#include <euphemus.h>
#include "euphemus_int.h"

typedef uint16_t eu_unescape_state_t;

static char unescape_table[UCHAR_MAX] CACHE_ALIGN = {
	['"'] = '\"',
	['\\'] = '\\',
	['/'] = '/',
	['b'] = '\b',
	['f'] = '\f',
	['n'] = '\n',
	['r'] = '\r',
	['t'] = '\t'
};

/* Copy characters from ep->input to dest, unescaping any escape sequences
 * encountered.  If the input ends with an incomplete escape sequence,
 * the state information is placed in ues. Returns the end of the
 * output characters, or NULL on error. */
static char *unescape(struct eu_parse *ep, const char *end, char *dest,
		      eu_unescape_state_t *ues)
{
	const char *p = ep->input;

	while (p != end) {
		if (*p != '\\') {
			*dest++ = *p++;
			continue;
		}

		if (++p != end) {
			char unescaped = unescape_table[(unsigned char)*p++];
			if (unescaped)
				*dest++ = unescaped;
			else
				/* bad escape sequence */
				return NULL;
		}
		else {
			*ues = 1;
			return dest;
		}
	}

	*ues = 0;
	return dest;
}

static eu_bool_t complete_unescape(struct eu_parse *ep,
				   eu_unescape_state_t *ues, char *out)
{
	if (ep->input != ep->input_end) {
		char unescaped = unescape_table[(unsigned char)*ep->input];
		if (!unescaped)
			/* bad escape sequence */
			return 0;

		*out = unescaped;
		*ues = 0;
		ep->input++;
	}
	else {
		*ues = 1;
	}

	return 1;
}

/* We've found a double-quotes character.  But was it escaped? Scan
   backwards counting backslashes to find out. This function is always
   entered with a double-quotes character preceding the string in the
   eu_parse input buffer, so we don't need to check for running off
   the start of the buffer. */
static eu_bool_t quotes_escaped(const char *p)
{
	size_t backslashes = 0;

	while (*--p == '\\')
		backslashes++;

	/* The double-quotes is escaped if preceded by an odd number
	   of backslashes. */
	return (backslashes & 1);
}

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
	struct string_parse_cont *cont = malloc(sizeof *cont);

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

		free(cont);
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
	eu_parse_insert_cont(ep, &cont->base);
	ep->input = p;
	return EU_PARSE_PAUSED;

 unescape:
	/* Skip the backslash, and scan forward to find the end of the
	   string */
	do {
		for (;;) {
			if (++p == end)
				goto pause_unescape;

			if (*p == '\"')
				break;
		}
	} while (quotes_escaped(p));

	len = p - ep->input;
	buf = malloc(len);
	if (!buf)
		goto alloc_error;

	{
		eu_unescape_state_t ues;
		end = unescape(ep, p, buf, &ues);
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

	end = unescape(ep, p, cont->buf, &cont->unescape);
	if (!end)
		return EU_PARSE_ERROR;

	cont->len = end - cont->buf;
	eu_parse_insert_cont(ep, &cont->base);
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
	eu_bool_t unescaped = 0;
	char unescaped_char;

	if (unlikely(cont->unescape)) {
		/* Unescaping has outcomes: finish, unfinished, and error */
		if (!complete_unescape(ep, &cont->unescape, &unescaped_char))
			return EU_PARSE_ERROR;

		if (cont->unescape) {
			eu_parse_insert_cont(ep, &cont->base);
			return EU_PARSE_PAUSED;
		}

		unescaped = 1;
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
	total_len = cont->len + len + unescaped;

	buf = cont->buf;
	if (total_len > cont->capacity) {
		buf = realloc(buf, total_len);
		if (!buf)
			goto alloc_error;

		cont->buf = buf;
		cont->capacity = total_len;
	}

	if (unlikely(unescaped))
		buf[cont->len++] = unescaped_char;

	memcpy(buf + cont->len, ep->input, len);

	if (unlikely(!assign_trimming(cont->result, buf, total_len,
				      cont->capacity)))
		goto alloc_error;

	ep->input = p + 1;
	free(cont);
	return EU_PARSE_OK;

 pause:
	len = p - ep->input;
	total_len = cont->len + len + unescaped;

	buf = cont->buf;
	if (total_len > cont->capacity) {
		size_t new_capacity = total_len * 2;
		buf = realloc(buf, new_capacity);
		if (!buf)
			goto alloc_error;

		cont->buf = buf;
		cont->capacity = new_capacity;
	}

	if (unlikely(unescaped))
		buf[cont->len++] = unescaped_char;

	memcpy(buf + cont->len, ep->input, len);
	cont->len = total_len;
	ep->input = p;
	eu_parse_insert_cont(ep, &cont->base);
	return EU_PARSE_PAUSED;

 unescape:
	/* Skip the backslash, and scan forward to find the end of the
	   string */
	do {
		for (;;) {
			if (++p == end)
				goto pause_unescape;

			if (*p == '\"')
				break;
		}
	} while (quotes_escaped(p));

	len = p - ep->input;
	total_len = cont->len + len + unescaped;

	buf = cont->buf;
	if (total_len > cont->capacity) {
		cont->capacity = total_len;
		buf = realloc(buf, total_len);
		if (!buf)
			goto alloc_error;
	}

	if (unlikely(unescaped))
		buf[cont->len++] = unescaped_char;

	{
		eu_unescape_state_t ues;
		end = unescape(ep, p, buf + cont->len, &ues);
		if (!end || ues)
			return EU_PARSE_ERROR;
	}

	if (unlikely(!assign_trimming(cont->result, buf, end - buf,
				      cont->capacity)))
		goto alloc_error;

	ep->input = p + 1;
	free(cont);
	return EU_PARSE_OK;

 pause_unescape:
	len = p - ep->input;
	total_len = cont->len + len + unescaped;

	buf = cont->buf;
	if (total_len > cont->capacity) {
		size_t new_capacity = total_len * 2;
		buf = realloc(buf, new_capacity);
		if (!buf)
			goto alloc_error;

		cont->buf = buf;
		cont->capacity = new_capacity;
	}

	if (unlikely(unescaped))
		buf[cont->len++] = unescaped_char;

	end = unescape(ep, p, buf + cont->len, &cont->unescape);
	cont->len = end - buf;
	ep->input = p;
	eu_parse_insert_cont(ep, &cont->base);
	return EU_PARSE_PAUSED;

 alloc_error:
	free(cont->buf);
	free(cont);
	return EU_PARSE_ERROR;
}

static void string_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *gcont)
{
	struct string_parse_cont *cont = (struct string_parse_cont *)gcont;

	(void)ep;

	free(cont->buf);
	free(cont);
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
