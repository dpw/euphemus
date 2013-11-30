#ifndef EUPHEMUS_EUPHEMUS_INT_H
#define EUPHEMUS_EUPHEMUS_INT_H

#ifdef __GNUC__
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

void eu_parse_insert_cont(struct eu_parse *ep, struct eu_parse_cont *c);
int eu_parse_set_member_name(struct eu_parse *ep, const char *start,
			     const char *end);
int eu_parse_append_member_name(struct eu_parse *ep, const char *start,
				const char *end);

extern struct eu_struct_metadata eu_inline_open_struct_metadata;

static __inline__ const char *skip_whitespace(const char *p, const char *end)
{
	/* Not doing UTF-8 yet, so no error or pause returns */

	for (; p != end; p++) {
		switch (*p) {
		case ' ':
		case '\f':
		case '\n':
		case '\r':
		case '\t':
		case '\v':
			break;

		default:
			goto out;
		}
	}

 out:
	return p;
}

enum eu_parse_result eu_consume_whitespace(struct eu_metadata *metadata,
					   struct eu_parse *ep,
					   void *result);

#endif
