#include <euphemus.h>
#include "euphemus_int.h"
#include "unescape.h"

struct eu_struct_member {
	unsigned int offset;
	unsigned short name_len;
	signed char presence_offset;
	unsigned char presence_bit;
	const char *name;
	const struct eu_metadata *metadata;
};

struct eu_struct_metadata {
	struct eu_metadata base;
	unsigned int struct_size;
	unsigned int extras_offset;
	unsigned int extra_member_size;
	unsigned int extra_member_value_offset;
	size_t n_members;
	const struct eu_struct_member *members;
	const struct eu_metadata *extra_value_metadata;
};

struct eu_generic_members {
	void *members;
	size_t len;

	struct {
		size_t capacity;
	} priv;
};

static const struct eu_metadata *add_extra(const struct eu_struct_metadata *md,
					   char *s, char *name, size_t name_len,
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
			size_t sz;

			capacity = 8;
			sz = capacity * md->extra_member_size;
			members	= malloc(sz);
			if (!members)
				goto err;

			memset(members, 0, sz);
		}
		else {
			size_t sz = capacity * md->extra_member_size;

			capacity *= 2;
			members = realloc(extras->members, 2 * sz);
			if (!members)
				goto err;

			memset(members + sz, 0, sz);
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
	*value = member + md->extra_member_value_offset;
	return md->extra_value_metadata;

 err:
	free(name);
	return NULL;
}

static const struct eu_metadata *lookup_member(
					const struct eu_struct_metadata *md,
					char *s, const char *name,
					const char *name_end, void **value)
{
	size_t name_len = name_end - name;
	size_t i;
	char *name_copy;

	for (i = 0; i < md->n_members; i++) {
		const struct eu_struct_member *m = &md->members[i];
		if (m->name_len != name_len)
			continue;

		if (!memcmp(m->name, name, name_len)) {
			if (m->presence_offset >= 0)
				s[m->presence_offset] |= m->presence_bit;

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

static const struct eu_metadata *lookup_member_2(
				const struct eu_struct_metadata *md, char *s,
				struct eu_string_ref buf,
				const char *more, const char *more_end,
				void **value)
{
	size_t more_len = more_end - more;
	size_t name_len = buf.len + more_len;
	size_t i;
	char *name_copy;

	for (i = 0; i < md->n_members; i++) {
		const struct eu_struct_member *m = &md->members[i];
		if (m->name_len != name_len)
			continue;

		if (!memcmp(m->name, buf.chars, buf.len)
		    && !memcmp(m->name + buf.len, more, more_len)) {
			if (m->presence_offset >= 0)
				s[m->presence_offset] |= m->presence_bit;

			*value = s + m->offset;
			return m->metadata;
		}
	}

	name_copy = malloc(name_len);
	if (!name_copy)
		return NULL;

	memcpy(name_copy, buf.chars, buf.len);
	memcpy(name_copy + buf.len, more, more_len);
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
	const struct eu_struct_metadata *metadata;
	void *result;
	void **result_ptr;
	const struct eu_metadata *member_metadata;
	void *member_value;
	eu_unescape_state_t unescape;
};

static enum eu_parse_result struct_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont);
static void struct_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *cont);
static enum eu_parse_result struct_parse(const struct eu_metadata *gmetadata,
					 struct eu_parse *ep, void *result,
					 void **result_ptr);

static enum eu_parse_result struct_ptr_parse(const struct eu_metadata *gmetadata,
					     struct eu_parse *ep, void *result)
{
	const struct eu_struct_metadata *metadata
		= (const struct eu_struct_metadata *)gmetadata;
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

static enum eu_parse_result inline_struct_parse(
					const struct eu_metadata *gmetadata,
					struct eu_parse *ep, void *result)
{
	enum eu_parse_result res
		= eu_consume_whitespace_until(gmetadata, ep, result, '{');

	if (unlikely(res != EU_PARSE_OK))
		return res;

	return struct_parse(gmetadata, ep, result, NULL);
}

static enum eu_parse_result struct_parse(const struct eu_metadata *gmetadata,
					 struct eu_parse *ep, void *result,
					 void **result_ptr)
{
	struct struct_parse_cont *cont;
	const struct eu_struct_metadata *metadata
		= (const struct eu_struct_metadata *)gmetadata;
	enum struct_parse_state state;
	const struct eu_metadata *member_metadata;
	void *member_value;
	eu_unescape_state_t unescape = 0;
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
	const struct eu_struct_metadata *metadata = cont->metadata;
	void *result = cont->result;
	void **result_ptr = cont->result_ptr;
	const struct eu_metadata *member_metadata = cont->member_metadata;
	void *member_value = cont->member_value;
	eu_unescape_state_t unescape = cont->unescape;
	const char *p, *end;

	if (unlikely(unescape)) {
		eu_unicode_char_t uc;

		if (!eu_parse_reserve_more_scratch(ep, UTF8_LONGEST))
			return EU_PARSE_ERROR;

		if (!eu_finish_unescape(ep, &unescape, &uc))
			return EU_PARSE_ERROR;

		if (unescape)
			/* We can't simply return
			   EU_PARSE_REINSTATE_PAUSED here because
			   reserve_scratch may have fiddled with the
			   stack. */
			goto pause_input_set;

		eu_stack_set_scratch_end(&ep->stack,
					 eu_unicode_to_utf8(uc,
					      eu_stack_scratch_end(&ep->stack)));
	}

	p = ep->input;
	end = ep->input_end;

#define RESUME_ONLY(x) x
	switch (state) {
#include "struct_sm.c"

	case STRUCT_PARSE_IN_MEMBER_NAME:
		/* The member name was split, so we need to accumulate
		   the compplete member name rather than simply
		   picking up where we left off. */
		for (;; p++) {
			if (p != end) {
				switch (*p) {
				case '\"': goto resume_member_name_done;
				case '\\': goto resume_unescape_member_name;
				default: break;
				}
			}
			else {
				if (!eu_parse_append_to_scratch(ep, ep->input,
								p))
					goto alloc_error;

				goto pause;
			}
		}

	resume_member_name_done:
		member_metadata = lookup_member_2(metadata, result,
						eu_stack_scratch_ref(&ep->stack),
						ep->input, p, &member_value);
		eu_parse_reset_scratch(ep);
		goto looked_up_member;

	resume_unescape_member_name:
		/* Skip the backslash, and scan forward to find the end of the
		   member name */
		do {
			if (++p == end)
				goto pause_resume_unescape_member_name;
		} while (*p != '\"' || quotes_escaped_bounded(p, ep->input));

		if (!eu_parse_reserve_more_scratch(ep, p - ep->input))
			goto error_input_set;

		{
			char *unescaped_end
				= eu_unescape(ep, p,
					      eu_stack_scratch_end(&ep->stack),
					      &unescape);
			if (!unescaped_end || unescape)
				goto error_input_set;

			member_metadata = lookup_member(metadata, result,
						  eu_stack_scratch(&ep->stack),
						  unescaped_end, &member_value);
			eu_parse_reset_scratch(ep);
		}

		goto looked_up_member;

	pause_resume_unescape_member_name:
		if (!eu_parse_reserve_more_scratch(ep, p - ep->input))
			goto error_input_set;

		{
			char *unescaped_end
				= eu_unescape(ep, p,
					      eu_stack_scratch_end(&ep->stack),
					      &unescape);
			if (!unescaped_end)
				goto error_input_set;

			eu_stack_set_scratch_end(&ep->stack, unescaped_end);
		}

		goto pause;

	default:
		goto error;
	}
#undef RESUME_ONLY
}

static void struct_extras_fini(const struct eu_struct_metadata *md,
			       void *v_extras)
{
	struct eu_generic_members *extras = v_extras;
	const struct eu_metadata *evmd = md->extra_value_metadata;
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

static void inline_struct_fini(const struct eu_metadata *gmetadata, void *s)
{
	const struct eu_struct_metadata *metadata
		= (const struct eu_struct_metadata *)gmetadata;
	size_t i;

	for (i = 0; i < metadata->n_members; i++) {
		const struct eu_struct_member *member = &metadata->members[i];
		member->metadata->fini(member->metadata,
				       (char *)s + member->offset);
	}

	struct_extras_fini(metadata, (char *)s + metadata->extras_offset);
}

static void struct_ptr_fini(const struct eu_metadata *gmetadata, void *value)
{
	void *s = *(void **)value;

	if (s) {
		inline_struct_fini(gmetadata, s);
		free(s);
		*(void **)value = NULL;
	}
}

void eu_struct_extras_fini(const struct eu_metadata *smd, void *v_extras)
{
	if (smd->fini != inline_struct_fini)
		abort();

	struct_extras_fini((const struct eu_struct_metadata *)smd, v_extras);
}

static void struct_parse_cont_destroy(struct eu_parse *ep,
				      struct eu_parse_cont *gcont)
{
	struct struct_parse_cont *cont = (struct struct_parse_cont *)gcont;

	(void)ep;

	inline_struct_fini(&cont->metadata->base, cont->result);
	if (cont->result_ptr) {
		free(cont->result);
		*cont->result_ptr = NULL;
	}
}

static __inline__ int struct_member_present(const struct eu_struct_member *m,
					    unsigned char *p)
{
	if (m->presence_offset >= 0)
		return !!(p[m->presence_offset] & m->presence_bit);
	else
		return !!*(void **)(p + m->offset);
}

static struct eu_value inline_struct_get(struct eu_value val,
					 struct eu_string_ref name)
{
	const struct eu_struct_metadata *md
		= (const struct eu_struct_metadata *)val.metadata;
	size_t i;
	unsigned char *s = val.value;
	struct eu_generic_members *extras;
	char *em;

	for (i = 0; i < md->n_members; i++) {
		const struct eu_struct_member *m = &md->members[i];
		if (m->name_len == name.len
		    && !memcmp(m->name, name.chars, name.len)) {
			if (struct_member_present(m, s))
				return eu_value(s + m->offset, m->metadata);
			else
				return eu_value_none;
		}
	}

	extras = (void *)(s + md->extras_offset);
	em = extras->members;
	for (i = 0; i < extras->len; i++) {
		if (eu_string_ref_equal(*(struct eu_string_ref *)em, name))
			return eu_value(em + md->extra_member_value_offset,
					md->extra_value_metadata);

		em += md->extra_member_size;
	}

	return eu_value_none;
}

static struct eu_value struct_ptr_get(struct eu_value val,
				      struct eu_string_ref name)
{
	val.value = *(void **)val.value;
	return inline_struct_get(val, name);
}

struct struct_iter_priv {
	struct eu_object_iter_priv base;

	unsigned int struct_i;
	unsigned char *struct_p;
	const struct eu_struct_member *m;

	size_t extras_i;
	char *extras_p;
	unsigned int extra_size;
	unsigned int extra_value_offset;
	const struct eu_metadata *extra_value_metadata;
};

static int struct_iter_next(struct eu_object_iter *iter)
{
	struct struct_iter_priv *priv = (struct struct_iter_priv *)iter->priv;

	while (priv->struct_i) {
		const struct eu_struct_member *m = priv->m++;
		priv->struct_i--;

		if (struct_member_present(m, priv->struct_p)) {
			iter->name = eu_string_ref(m->name, m->name_len);
			iter->value = eu_value(priv->struct_p + m->offset,
					       m->metadata);
			return 1;
		}
	}

	if (priv->extras_i) {
		iter->name = *(struct eu_string_ref *)priv->extras_p;
		iter->value = eu_value(priv->extras_p
				       + priv->extra_value_offset,
				       priv->extra_value_metadata);
		priv->extras_i--;
		priv->extras_p += priv->extra_size;
		return 1;
	}

	return 0;
}

static int inline_struct_iter_init(struct eu_value val,
				   struct eu_object_iter *iter)
{
	const struct eu_struct_metadata *md
		= (const struct eu_struct_metadata *)val.metadata;
	struct eu_generic_members *extras;
	struct struct_iter_priv *priv = malloc(sizeof *priv);

	if (!priv)
		return 0;

	priv->base.next = struct_iter_next;

	priv->struct_i = md->n_members;
	priv->struct_p = val.value;
	priv->m = md->members;

	extras = (void *)(priv->struct_p + md->extras_offset);
	priv->extras_i = extras->len;
	priv->extras_p = extras->members;
	priv->extra_size = md->extra_member_size;
	priv->extra_value_offset = md->extra_member_value_offset;
	priv->extra_value_metadata = md->extra_value_metadata;

	iter->priv = &priv->base;
	return 1;
}

static int struct_ptr_iter_init(struct eu_value val,
				struct eu_object_iter *iter)
{
	val.value = *(void **)val.value;
	return inline_struct_iter_init(val, iter);
}

int eu_object_iter_init(struct eu_object_iter *iter, struct eu_value val)
{
	iter->priv = NULL;
	return val.metadata->object_iter_init(val, iter);
}

int eu_object_iter_next(struct eu_object_iter *iter)
{
	if (!iter->priv)
		return 0;
	else
		return iter->priv->next(iter);
}

void eu_object_iter_fini(struct eu_object_iter *iter)
{
	free(iter->priv);
}

static size_t inline_struct_size(struct eu_value val)
{
	const struct eu_struct_metadata *md
		= (const struct eu_struct_metadata *)val.metadata;
	unsigned char *p = val.value;
	unsigned int i, count = 0;
	struct eu_generic_members *extras = (void *)(p + md->extras_offset);

	for (i = 0; i < md->n_members; i++)
		if (struct_member_present(md->members + i, p))
			count++;

	return count + extras->len;
}

static size_t struct_ptr_size(struct eu_value val)
{
	val.value = *(void **)val.value;
	return inline_struct_size(val);
}

size_t eu_object_size(struct eu_value val)
{
	return val.metadata->object_size(val);
}

const struct eu_struct_metadata eu_object_metadata = {
	{
		EU_JSON_OBJECT,
		sizeof(struct eu_object),
		inline_struct_parse,
		eu_generate_fail,
		inline_struct_fini,
		inline_struct_get,
		inline_struct_iter_init,
		inline_struct_size
	},
	-1,
	0,
	sizeof(struct eu_variant_member),
	offsetof(struct eu_variant_member, value),
	0,
	NULL,
	&eu_variant_metadata
};

enum eu_parse_result eu_variant_object(const void *unused_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result)
{
	(void)unused_metadata;
	result->metadata = &eu_object_metadata.base;
	return struct_parse(&eu_object_metadata.base, ep, &result->u.object,
			    NULL);
}

static int introduce_struct(struct eu_struct_descriptor_v1 *d,
			    struct eu_introduce_chain *chain)
{
	struct eu_introduce_chain struct_chain;
	struct eu_introduce_chain struct_ptr_chain;

	struct eu_struct_metadata *md = malloc(sizeof *md);
	struct eu_struct_metadata *pmd = malloc(sizeof *md);
	struct eu_struct_member *members
		= malloc(d->n_members * sizeof *members);
	size_t i;

	if (unlikely(md == NULL || pmd == NULL || members == NULL))
		goto error;

	struct_chain.descriptor = &d->struct_base;
	struct_chain.metadata = &md->base;
	struct_chain.next = &struct_ptr_chain;

	struct_ptr_chain.descriptor = &d->struct_ptr_base;
	struct_ptr_chain.metadata = &pmd->base;
	struct_ptr_chain.next = chain;

	chain = &struct_chain;

	md->base.json_type = pmd->base.json_type = EU_JSON_OBJECT;

	md->base.size = d->struct_size;
	md->base.parse = inline_struct_parse;
	md->base.generate = eu_generate_fail;
	md->base.fini = inline_struct_fini;
	md->base.get = inline_struct_get;
	md->base.object_iter_init = inline_struct_iter_init;
	md->base.object_size = inline_struct_size;

	pmd->base.size = sizeof(void *);
	pmd->base.parse = struct_ptr_parse;
	pmd->base.generate = eu_generate_fail;
	pmd->base.fini = struct_ptr_fini;
	pmd->base.get = struct_ptr_get;
	pmd->base.object_iter_init = struct_ptr_iter_init;
	pmd->base.object_size = struct_ptr_size;

	md->struct_size = pmd->struct_size = d->struct_size;
	md->extras_offset = pmd->extras_offset = d->extras_offset;
	md->extra_member_size = pmd->extra_member_size = d->extra_member_size;
	md->extra_member_value_offset = pmd->extra_member_value_offset
		= d->extra_member_value_offset;
	md->n_members = pmd->n_members = d->n_members;
	md->members = pmd->members = members;
	md->extra_value_metadata = pmd->extra_value_metadata
		= eu_introduce_aux(d->extra_value_descriptor, chain);
	if (!md->extra_value_metadata)
		goto error;

	for (i = 0; i < d->n_members; i++) {
		members[i].offset = d->members[i].offset;
		members[i].name_len = d->members[i].name_len;
		members[i].presence_offset = d->members[i].presence_offset;
		members[i].presence_bit = d->members[i].presence_bit;
		members[i].name = d->members[i].name;
		members[i].metadata = eu_introduce_aux(d->members[i].descriptor,
						       chain);
		if (!members[i].metadata)
			goto error;
	}

	*d->struct_base.metadata = &md->base;
	*d->struct_ptr_base.metadata = &pmd->base;
	return 1;

 error:
	free(md);
	free(pmd);
	free(members);
	return 0;
}

const struct eu_metadata *eu_introduce_struct(const struct eu_type_descriptor *d,
					      struct eu_introduce_chain *c)
{
	struct eu_struct_descriptor_v1 *sd
		= container_of(d, struct eu_struct_descriptor_v1, struct_base);
	if (introduce_struct(sd, c))
		return *d->metadata;
	else
		return NULL;
}

const struct eu_metadata *eu_introduce_struct_ptr(
					const struct eu_type_descriptor *d,
					struct eu_introduce_chain *c)
{
	struct eu_struct_descriptor_v1 *sd
	      = container_of(d, struct eu_struct_descriptor_v1, struct_ptr_base);
	if (introduce_struct(sd, c))
		return *d->metadata;
	else
		return NULL;
}
