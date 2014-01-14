#ifndef EUPHEMUS_EUPHEMUS_H
#define EUPHEMUS_EUPHEMUS_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum eu_json_type {
	EU_JSON_VARIANT,
	EU_JSON_STRING,
	EU_JSON_OBJECT,
	EU_JSON_ARRAY,
	EU_JSON_NUMBER,
	EU_JSON_BOOL,
	EU_JSON_NULL,
	EU_JSON_MAX
};

enum eu_parse_result {
	EU_PARSE_OK,
	EU_PARSE_PAUSED,
	EU_PARSE_ERROR
};

struct eu_parse;

struct eu_string_ref {
	const char *chars;
	size_t len;
};


static __inline__ struct eu_string_ref eu_string_ref(const char *chars,
						     size_t len)
{
	struct eu_string_ref s = { chars, len };
	return s;
}

static __inline__ struct eu_string_ref eu_cstr(const char *s)
{
	struct eu_string_ref f = {
		s,
		strlen(s)
	};

	return f;
}

static __inline__ int eu_string_ref_equal(struct eu_string_ref a,
					  struct eu_string_ref b)
{
	return a.len == b.len && !memcmp(a.chars, b.chars, a.len);
}

/* An eu_value pairs a pointer to some data representing a parsed JSON
   value with the metafata pointer allowing acces to it. */
struct eu_value {
	void *value;
	struct eu_metadata *metadata;
};

struct eu_object_iter;

/* A description of a type of data (including things like how to
   allocate, release etc.) */
struct eu_metadata {
	/* The EU_JSON_ value for this type. */
	unsigned char json_type;

	/* Size in bytes occupied by values of this type */
	unsigned int size;

	/* A parse function expects that there is at least one
	   character available in ep->input.  But it might be
	   whitespace.

	   The memory range allocated to the value is cleared before
	   this is called. */
	enum eu_parse_result (*parse)(struct eu_metadata *metadata,
				      struct eu_parse *ep,
				      void *result);

	/* Release any resources associated with the value.*/
	void (*fini)(struct eu_metadata *metadata, void *value);

	/* Get a member of an object or array. */
	struct eu_value (*get)(struct eu_value val, struct eu_string_ref name);

	void (*object_iter_init)(struct eu_value val,
				 struct eu_object_iter *iter);
};

static __inline__ struct eu_value eu_value(void *value,
					   struct eu_metadata *metadata)
{
	struct eu_value v = { value, metadata };
	return v;
}

static __inline__ int eu_value_ok(struct eu_value val)
{
	return !!val.metadata;
}

static const struct eu_value eu_value_none = { NULL, NULL };

static __inline__ struct eu_value eu_value_get(struct eu_value val,
					       struct eu_string_ref name)
{
	return val.metadata->get(val, name);
}

static __inline__ struct eu_value eu_value_get_cstr(struct eu_value val,
						    const char *name)
{
	return eu_value_get(val, eu_cstr(name));
}

/* Parsing */

struct eu_parse {
	struct eu_parse_cont *outer_stack;
	struct eu_parse_cont *stack_top;
	struct eu_parse_cont *stack_bottom;

	struct eu_metadata *metadata;
	void *result;

	const char *input;
	const char *input_end;

	char *buf;
	size_t buf_len;
	size_t buf_size;

	int error;
};

/* A continuation stack frame. */
struct eu_parse_cont {
	struct eu_parse_cont *next;
	enum eu_parse_result (*resume)(struct eu_parse *p,
				       struct eu_parse_cont *cont);
	void (*destroy)(struct eu_parse *ep, struct eu_parse_cont *cont);
};

void eu_parse_init(struct eu_parse *ep, struct eu_value result);
int eu_parse(struct eu_parse *ep, const char *input, size_t len);
int eu_parse_finish(struct eu_parse *ep);
void eu_parse_fini(struct eu_parse *ep);

enum eu_parse_result eu_parse_metadata_cont_resume(struct eu_parse *ep,
						   struct eu_parse_cont *cont);
void eu_parse_metadata_cont_destroy(struct eu_parse *ep,
				    struct eu_parse_cont *cont);

/* Path resolution */

struct eu_value eu_get_path(struct eu_value val, struct eu_string_ref path);

/* Variant objects */

#define EU_VARIANT_MEMBERS_DEFINED
struct eu_variant_members {
	struct eu_variant_member *members;
	size_t len;

	struct {
		size_t capacity;
	} priv;
};

struct eu_object {
	struct eu_variant_members members;
};

/* Strings */

struct eu_string {
	char *chars;
	size_t len;
};

static __inline__ struct eu_string_ref eu_string_to_ref(
						      struct eu_string *string)
{
	return eu_string_ref(string->chars, string->len);
}

static __inline__ void eu_string_fini(struct eu_string *string)
{
	free(string->chars);
	string->chars = NULL;
}

extern struct eu_metadata eu_string_metadata;

static __inline__ struct eu_value eu_string_value(struct eu_string *string)
{
	return eu_value(string, &eu_string_metadata);
}

/* Arrays */

struct eu_array {
	void *a;
	size_t len;
};

struct eu_array_metadata {
	struct eu_metadata base;
	struct eu_metadata *element_metadata;
};

enum eu_parse_result eu_array_parse(struct eu_metadata *gmetadata,
				    struct eu_parse *ep, void *result);
void eu_array_fini(struct eu_metadata *gmetadata, void *value);
struct eu_value eu_array_get(struct eu_value val, struct eu_string_ref name);
void eu_object_iter_init_fail(struct eu_value val, struct eu_object_iter *iter);

#define EU_ARRAY_METADATA_INITIALIZER(el_metadata)                    \
	{                                                             \
		{                                                     \
			EU_JSON_ARRAY,                                \
			sizeof(struct eu_array),                      \
			eu_array_parse,                               \
			eu_array_fini,                                \
			eu_array_get,                                 \
			eu_object_iter_init_fail                      \
                                                                      \
		},                                                    \
		el_metadata                                           \
	}

/* Others */

extern struct eu_metadata eu_number_metadata;

static __inline__ struct eu_value eu_number_value(double *number)
{
	return eu_value(number, &eu_number_metadata);
}


typedef unsigned char eu_bool_t;
extern struct eu_metadata eu_bool_metadata;

static __inline__ struct eu_value eu_bool_value(eu_bool_t *bool)
{
	return eu_value(bool, &eu_bool_metadata);
}

/* Variants */

struct eu_variant_array {
	struct eu_variant *a;
	size_t len;
};

struct eu_variant {
	struct eu_metadata *metadata;
	union {
		struct eu_string string;
		struct eu_object object;
		struct eu_variant_array array;
		double number;
		eu_bool_t bool;
	} u;
};

extern struct eu_metadata eu_variant_metadata;

static __inline__ struct eu_value eu_variant_value(struct eu_variant *variant)
{
	return eu_value(variant, &eu_variant_metadata);
}

static __inline__ void eu_variant_fini(struct eu_variant *variant)
{
	if (variant->metadata)
		variant->metadata->fini(variant->metadata, &variant->u);
}

static __inline__ enum eu_json_type eu_value_type(struct eu_value val)
{
	enum eu_json_type t = val.metadata->json_type;
	if (t != EU_JSON_VARIANT)
		return t;
	else
		return ((struct eu_variant *)val.value)->metadata->json_type;
}

static __inline__ void *eu_value_extract(struct eu_value val,
					 enum eu_json_type type)
{
	enum eu_json_type t = val.metadata->json_type;

	if (t == type)
		return val.value;

	if (t == EU_JSON_VARIANT) {
		struct eu_variant *var = val.value;
		if (var->metadata->json_type == type)
			return &var->u;
	}

	abort();
}

static __inline__ struct eu_string_ref eu_value_to_string_ref(
							   struct eu_value val)
{
	return eu_string_to_ref(eu_value_extract(val, EU_JSON_STRING));
}

static __inline__ double *eu_value_to_number(struct eu_value val)
{
	return eu_value_extract(val, EU_JSON_NUMBER);
}

static __inline__ eu_bool_t *eu_value_to_bool(struct eu_value val)
{
	return eu_value_extract(val, EU_JSON_BOOL);
}

static __inline__ struct eu_array *eu_value_to_array(struct eu_value val)
{
	return eu_value_extract(val, EU_JSON_ARRAY);
}

struct eu_variant_member {
	struct eu_string_ref name;
	struct eu_variant value;
};

/* Structs */

struct eu_struct_member {
	unsigned int offset;
	unsigned short name_len;
	signed char presence_offset;
	unsigned char presence_bit;
	const char *name;
	struct eu_metadata *metadata;
};

struct eu_struct_metadata {
	struct eu_metadata base;
	unsigned int struct_size;
	unsigned int extras_offset;
	unsigned int extra_member_size;
	unsigned int extra_member_value_offset;
	size_t n_members;
	struct eu_struct_member *members;
	struct eu_metadata *extra_value_metadata;
};

enum eu_parse_result eu_struct_parse(struct eu_metadata *gmetadata,
				     struct eu_parse *ep,
				     void *result);
void eu_struct_fini(struct eu_metadata *gmetadata, void *value);
struct eu_value eu_struct_get(struct eu_value val, struct eu_string_ref name);
void eu_struct_iter_init(struct eu_value val, struct eu_object_iter *iter);

enum eu_parse_result eu_struct_ptr_parse(struct eu_metadata *gmetadata,
					 struct eu_parse *ep,
					 void *result);
void eu_struct_ptr_fini(struct eu_metadata *gmetadata, void *value);
struct eu_value eu_struct_ptr_get(struct eu_value val,
				  struct eu_string_ref name);
void eu_struct_ptr_iter_init(struct eu_value val, struct eu_object_iter *iter);

void eu_struct_extras_fini(struct eu_struct_metadata *md, void *v_extras);
size_t eu_object_size(struct eu_value val);

#define EU_STRUCT_METADATA_INITIALIZER(struct_name, struct_members, extra_member_struct, extra_member_metadata) \
	{                                                             \
		{                                                     \
			EU_JSON_OBJECT,                               \
			sizeof(struct_name),                          \
			eu_struct_parse,                              \
			eu_struct_fini,                               \
			eu_struct_get,                                \
			eu_struct_iter_init                           \
		},                                                    \
		-1,                                                   \
		offsetof(struct_name, extras),                        \
		sizeof(extra_member_struct),                          \
		offsetof(extra_member_struct, value),                 \
		sizeof(struct_members) / sizeof(struct eu_struct_member), \
		struct_members,                                       \
		extra_member_metadata                                 \
	}

#define EU_STRUCT_PTR_METADATA_INITIALIZER(struct_name, struct_members, extra_member_struct, extra_member_metadata) \
	{                                                             \
		{                                                     \
			EU_JSON_OBJECT,                               \
			sizeof(struct_name *),                        \
			eu_struct_ptr_parse,                          \
			eu_struct_ptr_fini,                           \
			eu_struct_ptr_get,                            \
			eu_struct_ptr_iter_init                       \
		},                                                    \
		sizeof(struct_name),                                  \
		offsetof(struct_name, extras),                        \
		sizeof(extra_member_struct),                          \
		offsetof(extra_member_struct, value),                 \
		sizeof(struct_members) / sizeof(struct eu_struct_member), \
		struct_members,                                       \
		extra_member_metadata                                 \
	}

struct eu_object_iter {
	struct eu_string_ref name;
	struct eu_value value;

	struct {
		unsigned int struct_i;
		unsigned char *struct_p;
		struct eu_struct_member *m;

		size_t extras_i;
		char *extras_p;
		unsigned int extra_size;
		unsigned int extra_value_offset;
		struct eu_metadata *extra_value_metadata;
	} priv;
};

static __inline__ void eu_object_iter_init(struct eu_object_iter *iter,
					   struct eu_value val)
{
	val.metadata->object_iter_init(val, iter);
}

static __inline__ int eu_struct_member_present(struct eu_struct_member *m,
					       unsigned char *p)
{
	if (m->presence_offset >= 0)
		return !!(p[m->presence_offset] & m->presence_bit);
	else
		return !!*(void **)(p + m->offset);
}

static __inline__ int eu_object_iter_next(struct eu_object_iter *iter)
{
	while (iter->priv.struct_i) {
		struct eu_struct_member *m = iter->priv.m++;
		iter->priv.struct_i--;

		if (eu_struct_member_present(m, iter->priv.struct_p)) {
			iter->name = eu_string_ref(m->name, m->name_len);
			iter->value = eu_value(iter->priv.struct_p + m->offset,
					       m->metadata);
			return 1;
		}
	}

	if (iter->priv.extras_i) {
		iter->name = *(struct eu_string_ref *)iter->priv.extras_p;
		iter->value = eu_value(iter->priv.extras_p
				       + iter->priv.extra_value_offset,
				       iter->priv.extra_value_metadata);
		iter->priv.extras_i--;
		iter->priv.extras_p += iter->priv.extra_size;
		return 1;
	}
	else {
		return 0;
	}
}

#endif
