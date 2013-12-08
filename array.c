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

enum eu_parse_result eu_array_parse(struct eu_metadata *gmetadata,
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

	if (unlikely(*ep->input != '[')) {
		enum eu_parse_result res = eu_consume_whitespace(ep, gmetadata,
								 result);
		if (unlikely(res != EU_PARSE_OK))
			return res;

		if (unlikely(*ep->input != '['))
			return EU_PARSE_ERROR;
	}

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