#include "euphemus.h"
#include "euphemus_int.h"

struct eu_generic_members {
	void *members;
	size_t len;

	struct {
		size_t capacity;
	} priv;
};

static struct eu_metadata *add_extra(struct eu_struct_metadata *md, char *s,
				     char *name, size_t name_len,
				     void **value)
{
	struct eu_generic_members *extras = (void *)(s + md->extras_offset);
	size_t capacity = extras->priv.capacity;
	char *members, *member;

	if (extras->len < capacity) {
		members = extras->members;
	}
	else {
		if (!capacity) {
			capacity = 8;
			members	= malloc(capacity * md->extra_member_size);
		}
		else {
			capacity *= 2;
			members = realloc(extras->members,
					  capacity * md->extra_member_size);
		}

		if (!members) {
			free(name);
			return NULL;
		}

		extras->members = members;
		extras->priv.capacity = capacity;
	}

	member = members + extras->len++ * md->extra_member_size;

	/* The name is always the first field in the member struct */
	*(struct eu_string_ref *)member = eu_string_ref(name, name_len);

	memset(member + md->extra_member_value_offset, 0,
	       md->extra_member_size - md->extra_member_value_offset);
	*value = member + md->extra_member_value_offset;
	return md->extra_value_metadata;
}

void eu_struct_extras_fini(struct eu_struct_metadata *md,
			   void *v_extras)
{
	struct eu_generic_members *extras = v_extras;
	struct eu_metadata *evmd = md->extra_value_metadata;
	char *m = extras->members;
	size_t i;

	for (i = 0; i < extras->len; i++) {
		free((void *)((struct eu_string_ref *)m)->chars);
		evmd->fini(evmd, m + md->extra_member_value_offset);
		m += md->extra_member_size;
	}

	free(extras->members);
	extras->members = NULL;
	extras->len = 0;
}

static struct eu_metadata *lookup_member(struct eu_struct_metadata *md,
					 char *s, const char *name,
					 const char *name_end, void **value)
{
	size_t name_len = name_end - name;
	size_t i;
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
	size_t i;
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

/* A state name refers to the token that precedes it, except for
   STRUCT_PARSE_IN_MEMBER_NAME. */
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
static enum eu_parse_result struct_parse(struct eu_metadata *gmetadata,
					 struct eu_parse *ep, void *result,
					 void **result_ptr);

enum eu_parse_result eu_variant_object(void *object_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result)
{
	result->metadata = object_metadata;
	return struct_parse(object_metadata, ep, &result->u.object, NULL);
}

enum eu_parse_result eu_struct_parse(struct eu_metadata *gmetadata,
				     struct eu_parse *ep, void *result)
{
	struct eu_struct_metadata *metadata
		= (struct eu_struct_metadata *)gmetadata;
	void *s;
	enum eu_parse_result res
		= eu_consume_whitespace_until(gmetadata, ep, result, '{');

	if (unlikely(res != EU_PARSE_OK))
		return res;

	s = malloc(metadata->struct_size);
	if (s) {
		*(void **)result = s;
		memset(s, 0, metadata->struct_size);
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
	enum eu_parse_result res
		= eu_consume_whitespace_until(gmetadata, ep, result, '{');

	if (unlikely(res != EU_PARSE_OK))
		return res;

	return struct_parse(gmetadata, ep, result, NULL);
}

static enum eu_parse_result struct_parse(struct eu_metadata *gmetadata,
					 struct eu_parse *ep, void *result,
					 void **result_ptr)
{
	struct struct_parse_cont *cont;
	struct eu_struct_metadata *metadata
		= (struct eu_struct_metadata *)gmetadata;
	enum struct_parse_state state;
	struct eu_metadata *member_metadata;
	void *member_value;
	const char *p = ep->input + 1;
	const char *end = ep->input_end;

#define RESUME_ONLY(x)
#include "struct_sm.c"
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
#include "struct_sm.c"

	case STRUCT_PARSE_IN_MEMBER_NAME:
		/* The member name was split, so we can't simply
		   resume in this case. */
		for (;; p++) {
			if (p == end) {
				if (!eu_parse_append_buffer(ep, ep->input, p))
					goto alloc_error;

				goto pause;
			}

			if (*p == '\"')
				break;
		}

		member_metadata = lookup_member_2(metadata, result,
						  ep->buf, ep->buf_len,
						  ep->input, p, &member_value);
		goto looked_up_member;

	default:
		goto error;
	}
#undef RESUME_ONLY
}

void eu_inline_struct_fini(struct eu_metadata *gmetadata, void *s)
{
	struct eu_struct_metadata *metadata
		= (struct eu_struct_metadata *)gmetadata;
	size_t i;

	for (i = 0; i < metadata->n_members; i++) {
		struct eu_struct_member *member = &metadata->members[i];
		member->metadata->fini(member->metadata,
				       (char *)s + member->offset);
	}

	eu_struct_extras_fini(metadata, (char *)s + metadata->extras_offset);
}

void eu_struct_fini(struct eu_metadata *gmetadata, void *value)
{
	void *s = *(void **)value;

	if (s) {
		eu_inline_struct_fini(gmetadata, s);
		free(s);
		*(void **)value = NULL;
	}
}

static void struct_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *gcont)
{
	struct struct_parse_cont *cont = (struct struct_parse_cont *)gcont;

	(void)ep;

	eu_inline_struct_fini(&cont->metadata->base, cont->result);
	if (cont->result_ptr) {
		free(cont->result);
		*cont->result_ptr = NULL;
	}

	free(cont);
}

struct eu_struct_metadata eu_inline_object_metadata = {
	{
		eu_inline_struct_parse,
		eu_inline_struct_fini,
		eu_inline_struct_resolve,
		sizeof(struct eu_object),
		EU_JSON_OBJECT
	},
	-1,
	0,
	sizeof(struct eu_variant_member),
	offsetof(struct eu_variant_member, value),
	0,
	NULL,
	&eu_variant_metadata
};

struct eu_variant *eu_variant_members_get(struct eu_variant_members *members,
					  struct eu_string_ref name)
{
	size_t i;

	for (i = 0; i < members->len; i++) {
		struct eu_variant_member *m = &members->members[i];
		if (eu_string_ref_equal(m->name, name))
			return &m->value;
	}

	return NULL;
}

int eu_inline_struct_resolve(struct eu_value *val, struct eu_string_ref name)
{
	struct eu_struct_metadata *md
		= (struct eu_struct_metadata *)val->metadata;
	size_t i;
	char *s = val->value;
	struct eu_variant_members *extras;

	for (i = 0; i < md->n_members; i++) {
		struct eu_struct_member *m = &md->members[i];
		if (m->name_len == name.len
		    && !memcmp(m->name, name.chars, name.len)) {
			val->value = s + m->offset;
			val->metadata = m->metadata;
			return 1;
		}
	}

	extras = (void *)(s + md->extras_offset);
	for (i = 0; i < extras->len; i++) {
		struct eu_variant_member *m = &extras->members[i];
		if (eu_string_ref_equal(m->name, name)) {
			*val = eu_variant_value(&m->value);
			return 1;
		}
	}

	return 0;
}

int eu_struct_resolve(struct eu_value *val, struct eu_string_ref name)
{
	val->value = *(void **)val->value;
	return eu_inline_struct_resolve(val, name);
}

