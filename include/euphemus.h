#ifndef EUPHEMUS_EUPHEMUS_H
#define EUPHEMUS_EUPHEMUS_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum eu_json_type {
	EU_JSON_INVALID,
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

/* A description of a type of data (including things like how to
   allocate, release etc.) */
struct eu_metadata {
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

	/* Resolve a JSON pointer. */
	int (*resolve)(struct eu_value *val, struct eu_string_ref name);

	unsigned int size;
	unsigned char json_type;
};

static __inline__ enum eu_json_type eu_value_type(struct eu_value val)
{
	return val.metadata->json_type;
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

void eu_parse_init(struct eu_parse *ep, struct eu_metadata *metadata,
		   void *result);
int eu_parse(struct eu_parse *ep, const char *input, size_t len);
int eu_parse_finish(struct eu_parse *ep);
void eu_parse_fini(struct eu_parse *ep);

enum eu_parse_result eu_parse_metadata_cont_resume(struct eu_parse *ep,
						   struct eu_parse_cont *cont);
void eu_parse_metadata_cont_destroy(struct eu_parse *ep,
				    struct eu_parse_cont *cont);

/* Path resolution */

int eu_resolve_path(struct eu_value *val, struct eu_string_ref path);
int eu_resolve_error(struct eu_value *val, struct eu_string_ref name);

/* Variant objects */

#define EU_VARIANT_MEMBERS_DEFINED
struct eu_variant_members {
	struct eu_variant_member *members;
	size_t len;

	struct {
		size_t capacity;
	} priv;
};

struct eu_variant *eu_variant_members_get(struct eu_variant_members *members,
					  struct eu_string_ref name);

struct eu_object {
	struct eu_variant_members members;
};

static __inline__ struct eu_variant *eu_object_get(struct eu_object *obj,
						   struct eu_string_ref name)
{
	return eu_variant_members_get(&obj->members, name);
}

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
void eu_parse_init_string(struct eu_parse *ep, struct eu_string *str);

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
int eu_array_resolve(struct eu_value *val, struct eu_string_ref name);

#define EU_ARRAY_METADATA_INITIALIZER(el_metadata)                    \
	{                                                             \
		{                                                     \
			eu_array_parse,                               \
			eu_array_fini,                                \
			eu_array_resolve,                             \
			sizeof(struct eu_array),                      \
			EU_JSON_ARRAY                                 \
		},                                                    \
		el_metadata                                           \
	}

/* Others */

extern struct eu_metadata eu_number_metadata;
void eu_parse_init_number(struct eu_parse *ep, double *num);

typedef unsigned char eu_bool_t;
extern struct eu_metadata eu_bool_metadata;
void eu_parse_init_bool(struct eu_parse *ep, eu_bool_t *num);

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
void eu_parse_init_variant(struct eu_parse *ep, struct eu_variant *var);

static __inline__ enum eu_json_type eu_variant_type(struct eu_variant *variant)
{
	return variant->metadata->json_type;
}

static __inline__ struct eu_value eu_variant_value(struct eu_variant *variant)
{
	struct eu_value v = { &variant->u, variant->metadata };
	return v;
}

static __inline__ struct eu_value eu_value(void *value,
					   struct eu_metadata *metadata)
{
	struct eu_value v = { value, metadata };

	if (metadata == &eu_variant_metadata)
		v = eu_variant_value(value);

	return v;
}

struct eu_variant *eu_variant_get(struct eu_variant *variant,
				  struct eu_string_ref name);

static __inline__ struct eu_variant *eu_variant_get_cstr(
						    struct eu_variant *variant,
						    const char *name)
{
	return eu_variant_get(variant, eu_cstr(name));
}

static __inline__ void eu_variant_fini(struct eu_variant *variant)
{
	if (variant->metadata)
		variant->metadata->fini(variant->metadata, &variant->u);
}

struct eu_variant_member {
	struct eu_string_ref name;
	struct eu_variant value;
};

/* Structs */

struct eu_struct_member {
	unsigned int offset;
	unsigned int name_len;
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
int eu_struct_resolve(struct eu_value *val, struct eu_string_ref name);
enum eu_parse_result eu_inline_struct_parse(struct eu_metadata *gmetadata,
					    struct eu_parse *ep,
					    void *result);
void eu_inline_struct_fini(struct eu_metadata *gmetadata, void *value);
int eu_inline_struct_resolve(struct eu_value *val, struct eu_string_ref name);
void eu_struct_extras_fini(struct eu_struct_metadata *md, void *v_extras);


#define EU_STRUCT_METADATA_INITIALIZER(struct_name, struct_members, extra_member_struct, extra_member_metadata) \
	{                                                             \
		{                                                     \
			eu_struct_parse,                              \
			eu_struct_fini,                               \
			eu_struct_resolve,                            \
			sizeof(struct_name *),                        \
			EU_JSON_INVALID                               \
		},                                                    \
		sizeof(struct_name),                                  \
		offsetof(struct_name, extras),                        \
		sizeof(extra_member_struct),                          \
		offsetof(extra_member_struct, value),                 \
		sizeof(struct_members) / sizeof(struct eu_struct_member), \
		struct_members,                                       \
		extra_member_metadata                                 \
	}

#define EU_INLINE_STRUCT_METADATA_INITIALIZER(struct_name, struct_members, extra_member_struct, extra_member_metadata) \
	{                                                             \
		{                                                     \
			eu_inline_struct_parse,                       \
			eu_inline_struct_fini,                        \
			eu_inline_struct_resolve,                     \
			sizeof(struct_name),                          \
			EU_JSON_INVALID                               \
		},                                                    \
		-1,                                                   \
		offsetof(struct_name, extras),                        \
		sizeof(extra_member_struct),                          \
		offsetof(extra_member_struct, value),                 \
		sizeof(struct_members) / sizeof(struct eu_struct_member), \
		struct_members,                                       \
		extra_member_metadata                                 \
	}

#endif
