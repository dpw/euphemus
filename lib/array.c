#include <euphemus.h>
#include "euphemus_int.h"

struct eu_array_metadata {
	struct eu_metadata base;
	const struct eu_metadata *element_metadata;
};

enum array_parse_state {
	ARRAY_PARSE_OPEN,
	ARRAY_PARSE_ELEMENT,
	ARRAY_PARSE_COMMA
};

struct array_parse_cont {
	struct eu_parse_cont base;
	enum array_parse_state state;
	const struct eu_metadata *el_metadata;
	struct eu_array *result;
	size_t capacity;
};

static enum eu_parse_result array_parse_resume(struct eu_parse *ep,
					       struct eu_parse_cont *gcont);
static void array_parse_cont_destroy(struct eu_parse *ep,
				     struct eu_parse_cont *cont);

enum eu_parse_result array_parse(const struct eu_metadata *gmetadata,
				 struct eu_parse *ep,
				 void *v_result);

enum eu_parse_result eu_array_parse(const struct eu_metadata *gmetadata,
				    struct eu_parse *ep,
				    void *result)
{
	enum eu_parse_result res
		= eu_consume_whitespace_until(gmetadata, ep, result, '[');

	if (res == EU_PARSE_OK)
		return array_parse(gmetadata, ep, result);
	else
		return res;
}

enum eu_parse_result array_parse(const struct eu_metadata *gmetadata,
				 struct eu_parse *ep,
				 void *v_result)
{
	struct array_parse_cont *cont;
	const struct eu_array_metadata *metadata
		= (const struct eu_array_metadata *)gmetadata;
	enum array_parse_state state;
	const struct eu_metadata *el_metadata = metadata->element_metadata;
	size_t el_size = el_metadata->size;
	struct eu_array *result = v_result;
	size_t len = 0;
	char *el;
	size_t capacity;

	ep->input++;

#include "array_sm.c"
}

static enum eu_parse_result array_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont)
{
	struct array_parse_cont *cont = (struct array_parse_cont *)gcont;
	enum array_parse_state state = cont->state;
	const struct eu_metadata *el_metadata = cont->el_metadata;
	size_t el_size = el_metadata->size;
	struct eu_array *result = cont->result;
	size_t capacity = cont->capacity;
	size_t len = result->len;
	char *el = (char *)result->a + len * el_size;

	switch (state) {
#define RESUME
#include "array_sm.c"
#undef RESUME
	}

	/* Without -O, gcc incorrectly reports that control can reach
	   here. */
	abort();
}

static void array_fini(const struct eu_metadata *el_metadata,
		       struct eu_array *array)
{
	if (array->len) {
		char *el = array->a;
		size_t i;

		for (i = 0; i < array->len; i++) {
			el_metadata->fini(el_metadata, el);
			el += el_metadata->size;
		}

		free(array->a);
		array->a = NULL;
		array->len = 0;
	}
}

static void array_parse_cont_destroy(struct eu_parse *ep,
				     struct eu_parse_cont *gcont)
{
	struct array_parse_cont *cont = (struct array_parse_cont *)gcont;

	(void)ep;
	array_fini(cont->el_metadata, cont->result);
}

void eu_array_fini(const struct eu_metadata *gmetadata, void *value)
{
	struct eu_array_metadata *metadata
		= (struct eu_array_metadata *)gmetadata;
	array_fini(metadata->element_metadata, value);
}

struct eu_value eu_array_get(struct eu_value val, struct eu_string_ref name)
{
	struct eu_array_metadata *md = (struct eu_array_metadata *)val.metadata;
	struct eu_array *array = val.value;
	size_t index, i;
	unsigned char digit;

	if (name.len == 0 || name.chars[0] < '0' || name.chars[0] > '9')
		goto fail;

	index = name.chars[0] - '0';

	for (i = 1; i < name.len; i++) {
		if (name.chars[i] < '0' || name.chars[i] > '9')
			goto fail;

		if (index > ((size_t)-1)/10)
			goto fail;

		index *= 10;

		digit = name.chars[i] - '0';
		if (index > ((size_t)-1)-digit)
			goto fail;

		index += digit;
	}

	return eu_value((char *)array->a + index * md->element_metadata->size,
			md->element_metadata);

 fail:
	return eu_value_none;
}

const struct eu_array_metadata eu_variant_array_metadata = {
	{
		EU_JSON_ARRAY,
		sizeof(struct eu_array),
		eu_array_parse,
		eu_generate_fail,
		eu_array_fini,
		eu_array_get,
		eu_object_iter_init_fail,
		eu_object_size_fail
	},
	&eu_variant_metadata
};

enum eu_parse_result eu_variant_array(const void *unused_metadata,
				      struct eu_parse *ep,
				      struct eu_variant *result)
{
	(void)unused_metadata;
	result->metadata = &eu_variant_array_metadata.base;
	return array_parse(&eu_variant_array_metadata.base, ep,
			   &result->u.array);
}

const struct eu_metadata *eu_introduce_array(const struct eu_type_descriptor *d,
					     struct eu_introduce_chain *chain)
{
	struct eu_introduce_chain chain_head;
	struct eu_array_metadata *md = malloc(sizeof *md);
	struct eu_array_descriptor_v1 *ad
		= container_of(d, struct eu_array_descriptor_v1, base);

	if (md == NULL)
		return NULL;

	chain_head.descriptor = &ad->base;
	chain_head.metadata = &md->base;
	chain_head.next = chain;

	md->base.json_type = EU_JSON_ARRAY;
	md->base.size = sizeof(struct eu_array);
	md->base.parse = eu_array_parse;
	md->base.generate = eu_generate_fail;
	md->base.fini = eu_array_fini;
	md->base.get = eu_array_get;
	md->base.object_iter_init = eu_object_iter_init_fail;
	md->base.object_size = eu_object_size_fail;
	md->element_metadata = eu_introduce_aux(ad->element_descriptor,
						&chain_head);

	*d->metadata = &md->base;
	return &md->base;
}
