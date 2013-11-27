#include <string.h>

#include "euphemus.h"
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
static void string_parse_cont_dispose(struct eu_parse_cont *cont);

static enum eu_parse_result string_parse(struct eu_metadata *metadata,
					 struct eu_parse *ep,
					 void *v_result)
{
	const char *p = ep->input;
	const char *end = ep->input_end;
	struct eu_string *result = v_result;
	struct string_parse_cont *cont;

	(void)metadata;

	if (*p != '\"')
		goto error;

	ep->input = ++p;

	for (;; p++) {
		if (p == end)
			goto pause;

		if (*p == '\"')
			break;
	}

	result->len = p - ep->input;
	if (!(result->string = malloc(result->len)))
		goto alloc_error;

	memcpy(result->string, ep->input, result->len);
	ep->input = p + 1;
	return EU_PARSE_OK;

 pause:
	cont = malloc(sizeof *cont);
	if (!cont)
		goto alloc_error;

	cont->base.resume = string_parse_resume;
	cont->base.dispose = string_parse_cont_dispose;
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
 error:
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
	cont->result->string = buf;
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

static void string_parse_cont_dispose(struct eu_parse_cont *gcont)
{
	struct string_parse_cont *cont = (struct string_parse_cont *)gcont;

	free(cont->buf);
	free(cont);
}

static void string_dispose(struct eu_metadata *metadata, void *value)
{
	struct eu_string *str = value;

	(void)metadata;

	free(str->string);
}

struct eu_metadata eu_string_metadata = {
	EU_METADATA_BASE_INITIALIZER,
	string_parse,
	string_dispose
};

