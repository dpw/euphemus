#include <string.h>

#include "euphemus.h"
#include "euphemus_int.h"

static struct eu_metadata *add_extra(struct eu_struct_metadata *md, char *s,
				     char *name, size_t name_len,
				     void **value)
{
	struct eu_open_struct *open = (void *)(s + md->open_offset);
	struct eu_struct_extra *e = malloc(sizeof *e);
	if (!e) {
		free(name);
		return NULL;
	}

	e->name = name;
	e->name_len = name_len;
	e->next = open->extras;
	e->value.metadata = NULL;
	open->extras = e;
	*value = &e->value;
	return &eu_variant_metadata;
}

static struct eu_metadata *lookup_member(struct eu_struct_metadata *md,
					 char *s, const char *name,
					 const char *name_end, void **value)
{
	size_t name_len = name_end - name;
	int i;
	char *name_copy;

	for (i = 0; i < md->n_members; i++) {
		struct eu_struct_member *m = &md->members[i];
		if (m->name_len != name_len)
			continue;

		if (!memcmp(m->name, name, name_len)) {
			*value = s + m->offset;
			return m->metadata;
		}
	}

	name_copy = malloc(name_len);
	if (!name_copy)
		return NULL;

	memcpy(name_copy, name, name_len);
	return add_extra(md, s, name_copy, name_len, value);
}

static struct eu_metadata *lookup_member_2(struct eu_struct_metadata *md,
					   char *s, const char *buf,
					   size_t buf_len, const char *more,
					   const char *more_end,
					   void **value)
{
	size_t more_len = more_end - more;
	size_t name_len = buf_len + more_len;
	int i;
	char *name_copy;

	for (i = 0; i < md->n_members; i++) {
		struct eu_struct_member *m = &md->members[i];
		if (m->name_len != name_len)
			continue;

		if (!memcmp(m->name, buf, buf_len)
		    && !memcmp(m->name + buf_len, more, more_len)) {
			*value = s + m->offset;
			return m->metadata;
		}
	}

	name_copy = malloc(name_len);
	if (!name_copy)
		return NULL;

	memcpy(name_copy, buf, buf_len);
	memcpy(name_copy + buf_len, more, more_len);
	return add_extra(md, s, name_copy, name_len, value);
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
	struct eu_struct_metadata *metadata;
	void *result;
	void **result_ptr;
	struct eu_metadata *member_metadata;
	void *member_value;
};

static enum eu_parse_result struct_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont);
static void struct_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *cont);

/* This parses, allocating a fresh struct. */
enum eu_parse_result struct_parse(struct eu_metadata *gmetadata,
				  struct eu_parse *ep, void *result,
				  void **result_ptr)
{
	struct struct_parse_cont *cont;
	struct eu_struct_metadata *metadata
		= (struct eu_struct_metadata *)gmetadata;
	enum struct_parse_state state;
	struct eu_metadata *member_metadata;
	void *member_value;
	const char *p = ep->input;
	const char *end = ep->input_end;

	memset(result, 0, metadata->size);

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
		member_metadata = lookup_member(metadata, result, ep->input, \
						p, &member_value);    \
RESUME_ONLY(looked_up_member:)                                        \
		if (!member_metadata)                                 \
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
		switch (member_metadata->parse(member_metadata, ep,   \
					       member_value)) {       \
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
	if (!eu_parse_set_member_name(ep, ep->input, p))              \
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
	cont->base.destroy = struct_parse_cont_destroy;               \
	cont->state = state;                                          \
	cont->metadata = metadata;                                    \
	cont->result = result;                                        \
	cont->result_ptr = result_ptr;                                \
	cont->member_metadata = member_metadata;                      \
	cont->member_value = member_value;                            \
	eu_parse_insert_cont(ep, &cont->base);                        \
	return EU_PARSE_PAUSED;                                       \
                                                                      \
 alloc_error:                                                         \
 error:                                                               \
	ep->input = p;                                                \
 error_input_set:                                                     \
	if (result_ptr)                                               \
		*result_ptr = NULL;                                   \
                                                                      \
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
	struct eu_struct_metadata *metadata = cont->metadata;
	void *result = cont->result;
	void **result_ptr = cont->result_ptr;
	struct eu_metadata *member_metadata = cont->member_metadata;
	void *member_value = cont->member_value;
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
				if (!eu_parse_append_member_name(ep, ep->input,
								 p))
					goto alloc_error;

				goto pause;
			}

			if (*p == '\"')
				break;
		}

		member_metadata = lookup_member_2(metadata, result,
						  ep->member_name_buf,
						  ep->member_name_len,
						  ep->input, p, &member_value);
		goto looked_up_member;

	default:
		goto error;
	}
#undef RESUME_ONLY
}

enum eu_parse_result eu_struct_parse(struct eu_metadata *gmetadata,
				     struct eu_parse *ep, void *result)
{
	struct eu_struct_metadata *metadata
		= (struct eu_struct_metadata *)gmetadata;
	void *s = malloc(metadata->size);

	if (s) {
		*(void **)result = s;
		return struct_parse(gmetadata, ep, s, (void **)result);
	}
	else {
		*(void **)result = NULL;
		return EU_PARSE_ERROR;
	}
}

enum eu_parse_result eu_inline_struct_parse(struct eu_metadata *gmetadata,
					    struct eu_parse *ep, void *result)
{
	return struct_parse(gmetadata, ep, result, NULL);
}

void eu_inline_struct_destroy(struct eu_metadata *gmetadata, void *s)
{
	struct eu_struct_metadata *metadata
		= (struct eu_struct_metadata *)gmetadata;
	int i;

	for (i = 0; i < metadata->n_members; i++) {
		struct eu_struct_member *member = &metadata->members[i];
		member->metadata->destroy(member->metadata,
					  (char *)s + member->offset);
	}

	eu_open_struct_fini(
			  (struct eu_open_struct *)(s + metadata->open_offset));
}

void eu_struct_destroy(struct eu_metadata *gmetadata, void *value)
{
	void *s = *(void **)value;

	if (s) {
		eu_inline_struct_destroy(gmetadata, s);
		free(s);
		*(void **)value = NULL;
	}
}

static void struct_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *gcont)
{
	struct struct_parse_cont *cont = (struct struct_parse_cont *)gcont;

	(void)ep;

	eu_inline_struct_destroy(&cont->metadata->base, cont->result);
	if (cont->result_ptr) {
		free(cont->result);
		*cont->result_ptr = NULL;
	}

	free(cont);
}

struct eu_struct_metadata eu_open_struct_metadata = {
	{
		EU_METADATA_BASE_INITIALIZER,
		eu_struct_parse,
		eu_struct_destroy
	},
	sizeof(struct eu_open_struct),
	0,
	0,
	NULL
};


struct eu_struct_metadata eu_inline_open_struct_metadata = {
	{
		EU_METADATA_BASE_INITIALIZER,
		eu_inline_struct_parse,
		eu_inline_struct_destroy
	},
	sizeof(struct eu_open_struct),
	0,
	0,
	NULL
};

void eu_open_struct_fini(struct eu_open_struct *os)
{
	struct eu_struct_extra *extras = os->extras;

	while (extras) {
		struct eu_struct_extra *next = extras->next;
		eu_variant_fini(&extras->value);
		free(extras->name);
		free(extras);
		extras = next;
	}
}

struct eu_variant *eu_open_struct_get(struct eu_open_struct *os,
				      const char *name)
{
	struct eu_struct_extra *extras = os->extras;

	while (extras) {
		if (!strncmp(name, extras->name, extras->name_len)
		    && name[extras->name_len] == 0)
			return &extras->value;

		extras = extras->next;
	}

	return NULL;
}
