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

struct array_parse_frame {
	struct eu_stack_frame base;
	enum array_parse_state state;
	const struct eu_metadata *el_metadata;
	struct eu_array *result;
};

static enum eu_result array_parse_resume(struct eu_stack_frame *gframe,
					 void *v_ep);
static void array_parse_frame_destroy(struct eu_stack_frame *gframe);

static enum eu_result array_parse_aux(const struct eu_metadata *gmetadata,
				      struct eu_parse *ep, void *v_result)
{
	struct array_parse_frame *frame;
	const struct eu_array_metadata *metadata
		= (const struct eu_array_metadata *)gmetadata;
	enum array_parse_state state = ARRAY_PARSE_OPEN;
	const struct eu_metadata *el_metadata = metadata->element_metadata;
	size_t el_size = el_metadata->size;
	struct eu_array *result = v_result;
	size_t len = 0;
	size_t capacity = 8;
	char *el;

	ep->input++;

#define RESUME_ONLY(x)
#include "array_parse_sm.c"
}

static enum eu_result array_parse_resume(struct eu_stack_frame *gframe,
					 void *v_ep)
{
	struct array_parse_frame *frame = (struct array_parse_frame *)gframe;
	struct eu_parse *ep = v_ep;
	enum array_parse_state state = frame->state;
	const struct eu_metadata *el_metadata = frame->el_metadata;
	size_t el_size = el_metadata->size;
	struct eu_array *result = frame->result;
	size_t capacity = result->priv.capacity;
	size_t len = result->len;
	char *el = (char *)result->a + len * el_size;

	switch (state) {
#define RESUME_ONLY(x) x
#include "array_parse_sm.c"
	}

	/* Without -O, gcc incorrectly reports that execution can reach
	   here. */
	abort();
}

static enum eu_result array_parse(const struct eu_metadata *gmetadata,
				  struct eu_parse *ep, void *result)
{
	enum eu_result res
		= eu_consume_whitespace_until(gmetadata, ep, result, '[');

	if (res == EU_OK)
		return array_parse_aux(gmetadata, ep, result);
	else
		return res;
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
	}

	if (array->priv.capacity)
		free(array->a);
}

void eu_array_fini(const struct eu_metadata *gmetadata, void *value)
{
	struct eu_array_metadata *metadata
		= (struct eu_array_metadata *)gmetadata;
	array_fini(metadata->element_metadata, value);
}

static void array_parse_frame_destroy(struct eu_stack_frame *gframe)
{
	struct array_parse_frame *frame = (struct array_parse_frame *)gframe;

	array_fini(frame->el_metadata, frame->result);

	/* To avoid fini functions being called multiple times. */
	frame->result->a = NULL;
	frame->result->priv.capacity = frame->result->len = 0;
}

int eu_array_grow(struct eu_array *array, size_t el_size)
{
	size_t cap = array->priv.capacity;
	size_t len = array->len + 1;

	if (cap < len) {
		char *new_a;

		if (!array->priv.capacity) {
			cap = 8;
			new_a = malloc(el_size * cap);
			if (!new_a)
				return 0;

			memset(new_a, 0, el_size * 8);
		}
		else {
			size_t sz = array->priv.capacity * el_size;
			size_t new_sz = sz;

			do {
				cap *= 2;
				new_sz *= 2;
			} while (cap < len);

			new_a = realloc(array->a, new_sz);
			if (!new_a)
				return 0;

			memset(new_a + sz, 0, new_sz - sz);
		}

		array->a = new_a;
		array->priv.capacity = cap;
	}

	return 1;
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

enum array_gen_state {
	ARRAY_GEN_COMMA,
	ARRAY_GEN_ELEMENT
};

struct array_gen_frame {
	struct eu_stack_frame base;
	size_t i;
	char *el;
	const struct eu_metadata *el_md;
	enum array_gen_state state;
};

static enum eu_result array_gen_resume(struct eu_stack_frame *gframe,
				       void *v_eg);

static enum eu_result array_generate(const struct eu_metadata *gmetadata,
				     struct eu_generate *eg, void *value)
{
	const struct eu_array_metadata *metadata
		= (const struct eu_array_metadata *)gmetadata;
	struct eu_array *array = value;
	size_t i;
	char *el;
	const struct eu_metadata *el_md = metadata->element_metadata;
	enum array_gen_state state;
	struct array_gen_frame *frame;

	if (array->len == 0)
		return eu_fixed_gen_32(eg, 2, MULTICHAR_2('[',']'), "[]");

	/* There is always at least one char of space in the output buffer. */
	*eg->output++ = '[';

	i = array->len;
	el = array->a;

#define RESUME_ONLY(x)
#include "array_gen_sm.c"
}

static enum eu_result array_gen_resume(struct eu_stack_frame *gframe,
				       void *v_eg)
{
	struct eu_generate *eg = v_eg;
	struct array_gen_frame *frame = (struct array_gen_frame *)gframe;
	size_t i = frame->i;
	char *el = frame->el;
	const struct eu_metadata *el_md = frame->el_md;
	enum array_gen_state state = frame->state;

#define RESUME_ONLY(x) x
	switch (state) {
#include "array_gen_sm.c"

	default:
		goto error;
	}
}

const struct eu_array_metadata eu_variant_array_metadata = {
	{
		EU_JSON_ARRAY,
		sizeof(struct eu_array),
		array_parse,
		array_generate,
		eu_array_fini,
		eu_array_get,
		eu_object_iter_init_fail,
		eu_object_size_fail
	},
	&eu_variant_metadata
};

enum eu_result eu_variant_array(const void *unused_metadata,
				struct eu_parse *ep, struct eu_variant *result)
{
	(void)unused_metadata;
	result->metadata = &eu_variant_array_metadata.base;
	return array_parse_aux(&eu_variant_array_metadata.base, ep,
			       &result->u.array);
}

void eu_variant_array_fini(struct eu_variant_array *array)
{
	array_fini(&eu_variant_metadata, (struct eu_array *)array);
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
	md->base.parse = array_parse;
	md->base.generate = array_generate;
	md->base.fini = eu_array_fini;
	md->base.get = eu_array_get;
	md->base.object_iter_init = eu_object_iter_init_fail;
	md->base.object_size = eu_object_size_fail;
	md->element_metadata = eu_introduce_aux(ad->element_descriptor,
						&chain_head);

	*d->metadata = &md->base;
	return &md->base;
}
