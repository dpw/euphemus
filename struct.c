#include <string.h>

#include "euphemus.h"
#include "euphemus_int.h"

static struct struct_member *lookup_member(struct struct_metadata *md,
					   const char *name,
					   const char *name_end)
{
	unsigned int name_len = name_end - name;
	int i;

	for (i = 0; i < md->n_members; i++) {
		struct struct_member *m = &md->members[i];
		if (m->name_len != name_len)
			continue;

		if (!memcmp(m->name, name, name_len))
			return m;
	}

	return NULL;
}

static struct struct_member *lookup_member_2(struct struct_metadata *md,
					     const char *buf,
					     size_t buf_len,
					     const char *more,
					     const char *more_end)
{
	size_t more_len = more_end - more;
	size_t name_len = buf_len + more_len;
	int i;

	for (i = 0; i < md->n_members; i++) {
		struct struct_member *m = &md->members[i];
		if (m->name_len != name_len)
			continue;

		if (!memcmp(m->name, buf, buf_len)
		    && !memcmp(m->name + buf_len, more, more_len))
			return m;
	}

	return NULL;
}

/* A state name refers to the token the precedes it (except for
   *_IN_*). */
enum struct_parse_state {
	STRUCT_PARSE_OPEN,
	STRUCT_PARSE_IN_MEMBER_NAME,
	STRUCT_PARSE_MEMBER_NAME,
	STRUCT_PARSE_COLON,
	STRUCT_PARSE_MEMBER_VALUE,
	STRUCT_PARSE_COMMA
};

struct struct_parse_cont {
	struct eu_parse_cont base;
	enum struct_parse_state state;
	struct struct_metadata *metadata;
	char *s;
	struct struct_member *member;
};

static enum eu_parse_result struct_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont);
static void struct_parse_cont_dispose(struct eu_parse_cont *cont);

/* This parses, allocating a fresh struct. */
enum eu_parse_result struct_parse(struct eu_metadata *gmetadata,
					 struct eu_parse *ep,
					 void *result)
{
	struct struct_parse_cont *cont;
	struct struct_metadata *metadata = (struct struct_metadata *)gmetadata;
	enum struct_parse_state state;
	char *s = malloc(metadata->size);
	struct struct_member *member;
	enum eu_parse_result res;
	const char *p = ep->input;
	const char *end = ep->input_end;

	if (!s)
		goto alloc_error;

	memset(s, 0, metadata->size);
	*(void **)result = s;

	if (*p != '{')
		goto error;

	p++;

#define STRUCT_PARSE_BODY                                             \
	state = STRUCT_PARSE_OPEN;                                    \
RESUME_ONLY(case STRUCT_PARSE_OPEN:)                                  \
	p = skip_whitespace(p, end);				      \
	if (p == end)                                                 \
		goto pause;                                           \
                                                                      \
	switch (*p) {                                                 \
	case '\"':                                                    \
		break;                                                \
                                                                      \
	case '}':                                                     \
		goto done;                                            \
                                                                      \
	default:                                                      \
		goto error;                                           \
	}                                                             \
                                                                      \
	for (;;) {                                                    \
		/* Record the start of the member name */             \
		ep->input = ++p;                                      \
		for (;; p++) {                                        \
			if (p == end)                                 \
				goto pause_in_member_name;            \
                                                                      \
			if (*p == '\"')                               \
				break;                                \
		}                                                     \
                                                                      \
		member = lookup_member(metadata, ep->input, p);       \
RESUME_ONLY(looked_up_member:)                                        \
		if (!member)                                          \
			goto error;                                   \
                                                                      \
		p++;                                                  \
		state = STRUCT_PARSE_MEMBER_NAME;                     \
RESUME_ONLY(case STRUCT_PARSE_MEMBER_NAME:)                           \
		p = skip_whitespace(p, end);                          \
		if (p == end)                                         \
			goto pause;                                   \
                                                                      \
		if (*p != ':')                                        \
			goto error;                                   \
                                                                      \
		p++;                                                  \
		state = STRUCT_PARSE_COLON;                           \
RESUME_ONLY(case STRUCT_PARSE_COLON:)                                 \
		p = skip_whitespace(p, end);                          \
		if (p == end)                                         \
			goto pause;                                   \
                                                                      \
		state = STRUCT_PARSE_MEMBER_VALUE;                    \
		ep->input = p;                                        \
		res = member->metadata->parse(member->metadata, ep,   \
		                              s + member->offset);    \
		switch (res) {                                        \
		case EU_PARSE_OK:                                     \
			break;                                        \
                                                                      \
		case EU_PARSE_PAUSED:                                 \
			goto pause_input_set;                         \
                                                                      \
		default:                                              \
			goto error_input_set;                         \
		}                                                     \
                                                                      \
		end = ep->input_end;                                  \
RESUME_ONLY(case STRUCT_PARSE_MEMBER_VALUE:)                          \
		p = skip_whitespace(ep->input, end);                  \
		if (p == end)                                         \
			goto pause;                                   \
                                                                      \
		switch (*p) {                                         \
		case ',':                                             \
			break;                                        \
                                                                      \
		case '}':                                             \
			goto done;                                    \
                                                                      \
		default:                                              \
			goto error;                                   \
		}                                                     \
                                                                      \
		p++;                                                  \
		state = STRUCT_PARSE_COMMA;                           \
RESUME_ONLY(case STRUCT_PARSE_COMMA:)                                 \
		p = skip_whitespace(p, end);                          \
		if (p == end)                                         \
			goto pause;                                   \
                                                                      \
		if (*p != '\"')                                       \
			goto error;                                   \
	}                                                             \
                                                                      \
 done:                                                                \
	ep->input = p + 1;                                            \
	return EU_PARSE_OK;                                           \
                                                                      \
 pause_in_member_name:                                                \
	state = STRUCT_PARSE_IN_MEMBER_NAME;                          \
	if (!set_member_name(ep, ep->input, p))                       \
		goto alloc_error;                                     \
                                                                      \
 pause:                                                               \
	ep->input = p;                                                \
 pause_input_set:                                                     \
	cont = malloc(sizeof *cont);                                  \
	if (!cont)                                                    \
		goto alloc_error;                                     \
                                                                      \
	cont->base.resume = struct_parse_resume;                      \
	cont->base.dispose = struct_parse_cont_dispose;               \
	cont->state = state;                                          \
	cont->metadata = metadata;                                    \
	cont->s = s;                                                  \
	cont->member = member;                                        \
	insert_cont(ep, &cont->base);                                 \
	return EU_PARSE_PAUSED;                                       \
                                                                      \
 alloc_error:                                                         \
 error:                                                               \
	ep->input = p;                                                \
 error_input_set:                                                     \
	return EU_PARSE_ERROR;

#define RESUME_ONLY(x)
	STRUCT_PARSE_BODY
#undef RESUME_ONLY
}

static enum eu_parse_result struct_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont)
{
	struct struct_parse_cont *cont = (struct struct_parse_cont *)gcont;
	enum struct_parse_state state = cont->state;
	struct struct_metadata *metadata = cont->metadata;
	char *s = cont->s;
	struct struct_member *member = cont->member;
	enum eu_parse_result res;
	const char *p = ep->input;
	const char *end = ep->input_end;

	free(cont);

#define RESUME_ONLY(x) x
	switch (state) {
	STRUCT_PARSE_BODY

	case STRUCT_PARSE_IN_MEMBER_NAME:
		/* The member name was split, so we can't simply
		   resume in this case. */
		for (;; p++) {
			if (p == end) {
				if (!append_member_name(ep, ep->input, p))
					goto alloc_error;

				goto pause;
			}

			if (*p == '\"')
				break;
		}

		member = lookup_member_2(metadata, ep->member_name_buf,
					 ep->member_name_len, ep->input, p);
		goto looked_up_member;

	default:
		goto error;
	}
#undef RESUME_ONLY
}

static void struct_free(struct struct_metadata *metadata, char *s)
{
	int i;

	if (!s)
		return;

	for (i = 0; i < metadata->n_members; i++) {
		struct struct_member *member = &metadata->members[i];
		member->metadata->dispose(member->metadata, s + member->offset);
	}

	free(s);
}

void struct_dispose(struct eu_metadata *gmetadata, void *value)
{
	struct_free((struct struct_metadata *)gmetadata,
		    *(void **)value);
}

static void struct_parse_cont_dispose(struct eu_parse_cont *gcont)
{
	struct struct_parse_cont *cont = (struct struct_parse_cont *)gcont;
	struct_free(cont->metadata, cont->s);
	free(cont);
}
