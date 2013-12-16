#ifndef EUPHEMUS_EUPHEMUS_INT_H
#define EUPHEMUS_EUPHEMUS_INT_H

#include <string.h>

#ifdef __GNUC__
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

void eu_parse_insert_cont(struct eu_parse *ep, struct eu_parse_cont *c);
int eu_parse_set_buffer(struct eu_parse *ep, const char *start,
			const char *end);
int eu_parse_append_buffer(struct eu_parse *ep, const char *start,
			   const char *end);
int eu_parse_append_buffer_nul(struct eu_parse *ep, const char *start,
			       const char *end);

void eu_noop_fini(struct eu_metadata *metadata, void *value);

extern struct eu_struct_metadata eu_inline_object_metadata;
extern struct eu_array_metadata eu_variant_array_metadata;
extern struct eu_metadata eu_null_metadata;
extern struct eu_bool_misc eu_bool_true;
extern struct eu_bool_misc eu_bool_false;

enum eu_parse_result eu_variant_string(void *string_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result);
enum eu_parse_result eu_variant_object(void *object_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result);
enum eu_parse_result eu_variant_array(void *array_metadata,
				      struct eu_parse *ep,
				      struct eu_variant *result);
enum eu_parse_result eu_variant_number(void *number_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result);
enum eu_parse_result eu_variant_bool(void *misc, struct eu_parse *ep,
				     struct eu_variant *result);
enum eu_parse_result eu_variant_n(void *null_metadata, struct eu_parse *ep,
				  struct eu_variant *result);

#define WHITESPACE_CASES ' ': case '\t': case '\n': case '\r'

static __inline__ const char *skip_whitespace(const char *p, const char *end)
{
	/* Not doing UTF-8 yet, so no error or pause returns */

	for (; p != end; p++) {
		switch (*p) {
		case WHITESPACE_CASES:
			break;

		default:
			goto out;
		}
	}

 out:
	return p;
}

enum eu_parse_result eu_insert_whitespace_cont(struct eu_metadata *metadata,
					       struct eu_parse *ep,
					       void *result);

static __inline__ enum eu_parse_result eu_consume_whitespace(
						struct eu_metadata *metadata,
						struct eu_parse *ep,
						void *result)
{
	const char *end = ep->input_end;
	ep->input = skip_whitespace(ep->input, end);
	if (ep->input != end)
		return EU_PARSE_OK;
	else
		return eu_insert_whitespace_cont(metadata, ep, result);
}

enum eu_parse_result eu_consume_ws_until_slow(struct eu_metadata *metadata,
					      struct eu_parse *ep,
					      void *result,
					      char c);

static __inline__ enum eu_parse_result eu_consume_whitespace_until(
						struct eu_metadata *metadata,
						struct eu_parse *ep,
						void *result,
						char c)
{
	if (unlikely(*ep->input != c)) {
		enum eu_parse_result res
			= eu_consume_ws_until_slow(metadata, ep, result, c);
		if (unlikely(res != EU_PARSE_OK))
			return res;
	}

	return EU_PARSE_OK;
}

enum eu_parse_result eu_parse_expect_pause(struct eu_parse *ep,
					   const char *expect,
					   size_t expect_len);

static __inline__ enum eu_parse_result eu_parse_expect(struct eu_parse *ep,
						       const char *expect,
						       size_t expect_len)
{
	size_t avail = ep->input_end - ep->input;

	if (avail >= expect_len) {
		if (!memcmp(ep->input, expect, expect_len)) {
			ep->input += expect_len;
			return EU_PARSE_OK;
		}
		else {
			return EU_PARSE_ERROR;
		}
	}
	else {
		return eu_parse_expect_pause(ep, expect, expect_len);
	}
}

#endif
