#include "euphemus.h"
#include "euphemus_int.h"

enum array_parse_state {
	ARRAY_PARSE_OPEN,
	ARRAY_PARSE_ELEMENT
};

struct array_parse_cont {
	struct eu_parse_cont base;
	enum array_parse_state state;
	struct eu_metadata *el_metadata;
	struct eu_array *result;
	size_t capacity;
};

static enum eu_parse_result array_parse_resume(struct eu_parse *ep,
					       struct eu_parse_cont *gcont);
static void array_parse_cont_destroy(struct eu_parse *ep,
				     struct eu_parse_cont *cont);

enum eu_parse_result array_parse(struct eu_metadata *gmetadata,
				 struct eu_parse *ep,
				 void *v_result);

enum eu_parse_result eu_variant_array(void *array_metadata,
				      struct eu_parse *ep,
				      struct eu_variant *result)
{
	result->metadata = array_metadata;
	return array_parse(array_metadata, ep, &result->u.array);
}

enum eu_parse_result eu_array_parse(struct eu_metadata *gmetadata,
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

enum eu_parse_result array_parse(struct eu_metadata *gmetadata,
				 struct eu_parse *ep,
				 void *v_result)
{
	struct array_parse_cont *cont;
	struct eu_array_metadata *metadata
		= (struct eu_array_metadata *)gmetadata;
	enum array_parse_state state;
	struct eu_metadata *el_metadata = metadata->element_metadata;
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
	struct eu_metadata *el_metadata = cont->el_metadata;
	size_t el_size = el_metadata->size;
	struct eu_array *result = cont->result;
	size_t capacity = cont->capacity;
	size_t len = result->len;
	char *el = result->a + len * el_size;

	free(cont);

	switch (state) {
#define RESUME
#include "array_sm.c"
#undef RESUME
	}

	/* Without -O, gcc incorrectly reports that control can reach
	   here. */
	abort();
}

static void array_fini(struct eu_metadata *el_metadata, struct eu_array *array)
{
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

static void array_parse_cont_destroy(struct eu_parse *ep,
				     struct eu_parse_cont *gcont)
{
	struct array_parse_cont *cont = (struct array_parse_cont *)gcont;

	(void)ep;
	array_fini(cont->el_metadata, cont->result);
	free(cont);
}

void eu_array_fini(struct eu_metadata *gmetadata, void *value)
{
	struct eu_array_metadata *metadata
		= (struct eu_array_metadata *)gmetadata;
	array_fini(metadata->element_metadata, value);
}

int eu_array_resolve(struct eu_value *val, struct eu_string_ref name)
{
	struct eu_array_metadata *md
		= (struct eu_array_metadata *)val->metadata;
	struct eu_array *array = val->value;
	size_t index, i;

	if (name.len == 0 || name.chars[0] < '0' || name.chars[0] > '9')
		return 0;

	index = name.chars[0] - '0';

	for (i = 1; i < name.len; i++) {
		if (name.chars[i] < '0' || name.chars[i] > '9')
			return 0;

		index = index * 10 + (name.chars[i] - '0');
	}

	*val = eu_value((char *)array->a + index * md->element_metadata->size,
			md->element_metadata);
	return 1;
}

struct eu_array_metadata eu_variant_array_metadata
	= EU_ARRAY_METADATA_INITIALIZER(&eu_variant_metadata);
