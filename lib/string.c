#include <euphemus.h>
#include "euphemus_int.h"

struct string_parse_cont {
	struct eu_parse_cont base;
	struct eu_string *result;
	char *buf;
	size_t len;
	size_t capacity;
};

static enum eu_parse_result string_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont);
static void string_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *cont);

static enum eu_parse_result string_parse_common(struct eu_metadata *metadata,
						struct eu_parse *ep,
						void *v_result);

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

static enum eu_parse_result string_parse_common(struct eu_metadata *metadata,
						struct eu_parse *ep,
						void *v_result)
{
	const char *p = ep->input;
	const char *end = ep->input_end;
	struct eu_string *result = v_result;
	struct string_parse_cont *cont;
	size_t len;

	(void)metadata;

	ep->input = ++p;

	for (;; p++) {
		if (p == end)
			goto pause;

		if (*p == '\"')
			break;
	}

	len = p - ep->input;
	if (!(result->chars = malloc(len)))
		goto alloc_error;

	memcpy(result->chars, ep->input, len);
	result->len = len;
	ep->input = p + 1;
	return EU_PARSE_OK;

 pause:
	cont = malloc(sizeof *cont);
	if (!cont)
		goto alloc_error;

	cont->base.resume = string_parse_resume;
	cont->base.destroy = string_parse_cont_destroy;
	cont->result = result;
	cont->len = p - ep->input;
	cont->capacity = cont->len * 2;
	cont->buf = malloc(cont->capacity);
	if (cont->buf) {
		memcpy(cont->buf, ep->input, cont->len);
		ep->input = p;
		eu_parse_insert_cont(ep, &cont->base);
		return EU_PARSE_PAUSED;
	}

	free(cont);

 alloc_error:
	ep->input = p;
	return EU_PARSE_ERROR;
}

static enum eu_parse_result string_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont)
{
	struct string_parse_cont *cont = (struct string_parse_cont *)gcont;
	const char *p = ep->input;
	const char *end = ep->input_end;
	char *buf;
	size_t len, total_len;

	for (;; p++) {
		if (p == end)
			goto pause;

		if (*p == '\"')
			break;
	}

	len = p - ep->input;
	total_len = cont->len + len;
	if (total_len <= cont->capacity) {
		buf = cont->buf;
	}
	else {
		buf = realloc(cont->buf, total_len);
		if (!buf)
			goto alloc_error;
	}

	memcpy(buf + cont->len, ep->input, len);
	cont->result->chars = buf;
	cont->result->len = total_len;
	ep->input = p + 1;
	free(cont);
	return EU_PARSE_OK;

 pause:
	len = p - ep->input;
	total_len = cont->len + len;
	if (total_len > cont->capacity) {
		size_t new_capacity = total_len * 2;
		buf = realloc(cont->buf, new_capacity);
		if (!buf)
			goto alloc_error;

		cont->buf = buf;
		cont->capacity = new_capacity;
	}

	memcpy(cont->buf + cont->len, ep->input, len);
	cont->len = total_len;
	ep->input = p + 1;
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

static void string_fini(struct eu_metadata *metadata, void *value)
{
	struct eu_string *str = value;
	(void)metadata;

	eu_string_fini(str);
}

struct eu_metadata eu_string_metadata = {
	string_parse,
	string_fini,
	eu_resolve_error,
	sizeof(struct eu_string),
	EU_JSON_STRING
};

void eu_parse_init_string(struct eu_parse *ep, struct eu_string *str)
{
	eu_parse_init(ep, &eu_string_metadata, str);
}