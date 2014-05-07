#ifndef EUPHEMUS_EUPHEMUS_H
#define EUPHEMUS_EUPHEMUS_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum eu_json_type {
	EU_JSON_INVALID,
	EU_JSON_VARIANT,
	EU_JSON_STRING,
	EU_JSON_OBJECT,
	EU_JSON_ARRAY,
	EU_JSON_NUMBER,
	EU_JSON_BOOL,
	EU_JSON_NULL,
	EU_JSON_MAX
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

static __inline__ int eu_string_ref_ok(struct eu_string_ref s)
{
	return !!s.chars;
}

static const struct eu_string_ref eu_string_ref_null = { NULL, 0 };

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

/* An eu_metadata is an opaque runtime representation of a type. These
   get generated from eu_type_descriptor structures which are the
   build-time representation.  This separation is intended to
   facilitate binary compatibility while retaining flexibility in the
   representations used. */
struct eu_metadata;

struct eu_type_descriptor {
	const struct eu_metadata **metadata;
	enum {
		EU_TDESC_SHIM,
		EU_TDESC_STRUCT_PTR_V1,
		EU_TDESC_STRUCT_V1,
		EU_TDESC_ARRAY_V1,
	} kind;
};

/* eu_introduce gets or builds the eu_metadata instance for an
   eu_type_descriptor. */
const struct eu_metadata *eu_introduce(const struct eu_type_descriptor *d);

extern const struct eu_type_descriptor eu_string_descriptor;
extern const struct eu_type_descriptor eu_number_descriptor;
extern const struct eu_type_descriptor eu_bool_descriptor;
extern const struct eu_type_descriptor eu_null_descriptor;
extern const struct eu_type_descriptor eu_variant_descriptor;

/* An eu_value pairs a pointer to some data representing a parsed JSON
   value with the metafata pointer allowing acces to it. */
struct eu_value {
	void *value;
	const struct eu_metadata *metadata;
};

struct eu_object_iter;

static __inline__ struct eu_value eu_value(void *value,
					   const struct eu_metadata *metadata)
{
	struct eu_value v = { value, metadata };
	return v;
}

static __inline__ int eu_value_ok(struct eu_value val)
{
	return !!val.metadata;
}

static const struct eu_value eu_value_none = { NULL, NULL };

struct eu_value eu_value_get(struct eu_value val, struct eu_string_ref name);

static __inline__ struct eu_value eu_value_get_cstr(struct eu_value val,
						    const char *name)
{
	return eu_value_get(val, eu_cstr(name));
}

/* Parsing */

struct eu_parse;

struct eu_parse *eu_parse_create(struct eu_value result);
int eu_parse(struct eu_parse *ep, const char *input, size_t len);
int eu_parse_finish(struct eu_parse *ep);
void eu_parse_destroy(struct eu_parse *ep);

/* Generation */

struct eu_generate;

struct eu_generate *eu_generate_create(struct eu_value value);

/* Returns the number of bytes produced.  If the output buffer was not
   filled, then either generation is complete or an error occured.
   Use eu_generate_ok to find out which. */
size_t eu_generate(struct eu_generate *eg, char *output, size_t len);

int eu_generate_ok(struct eu_generate *eg);
void eu_generate_destroy(struct eu_generate *eg);

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

static __inline__ void eu_object_init(struct eu_object *obj)
{
	memset(obj, 0, sizeof *obj);
}

void eu_object_fini(struct eu_object *obj);
struct eu_value eu_object_value(struct eu_object *obj);

struct eu_variant *eu_object_get(struct eu_object *obj,
				 struct eu_string_ref name);

/* Strings */

struct eu_string {
	char *chars;
	size_t len;
};

static __inline__ struct eu_string_ref eu_string_to_ref(struct eu_string *str)
{
	return eu_string_ref(str->chars, str->len);
}

static __inline__ int eu_string_init(struct eu_string *str,
				     struct eu_string_ref s)
{
	if ((str->chars = strdup(s.chars))) {
		str->len = s.len;
		return 1;
	}
	else {
		str->len = 0;
		return 0;
	}
}

static __inline__ void eu_string_fini(struct eu_string *str)
{
	if (str->len)
		free(str->chars);

	str->chars = NULL;
}

static __inline__ void eu_string_reset(struct eu_string *str)
{
	if (str->len)
		free(str->chars);

	str->chars = NULL;
	str->len = 0;
}

static __inline__ int eu_string_assign(struct eu_string *str,
				       struct eu_string_ref s)
{
	if (str->len)
		free(str->chars);

	if ((str->chars = strdup(s.chars))) {
		str->len = s.len;
		return 1;
	}
	else {
		str->len = 0;
		return 0;
	}
}

extern const struct eu_metadata eu_string_metadata;

static __inline__ struct eu_value eu_string_value(struct eu_string *str)
{
	return eu_value(str, &eu_string_metadata);
}

/* Arrays */

struct eu_array_descriptor_v1 {
	struct eu_type_descriptor base;
	const struct eu_type_descriptor *element_descriptor;
};

struct eu_array {
	void *a;
	size_t len;

	struct {
		size_t capacity;
	} priv;
};

void eu_array_fini(const struct eu_metadata *gmetadata, void *value);
struct eu_value eu_array_get(struct eu_value val, struct eu_string_ref name);
int eu_array_grow(struct eu_array *array, size_t len, size_t el_size);

/* Others */

extern const struct eu_metadata eu_number_metadata;

static __inline__ struct eu_value eu_number_value(double *number)
{
	return eu_value(number, &eu_number_metadata);
}


typedef unsigned char eu_bool_t;
extern const struct eu_metadata eu_bool_metadata;

static __inline__ struct eu_value eu_bool_value(eu_bool_t *bool)
{
	return eu_value(bool, &eu_bool_metadata);
}

extern const struct eu_metadata eu_null_metadata;

static __inline__ struct eu_value eu_null_value(void)
{
	return eu_value(NULL, &eu_null_metadata);
}

/* Variants */

struct eu_variant_array {
	struct eu_variant *a;
	size_t len;

	struct {
		size_t capacity;
	} priv;
};

struct eu_variant {
	const struct eu_metadata *metadata;
	union {
		struct eu_string string;
		struct eu_object object;
		struct eu_variant_array array;
		double number;
		eu_bool_t bool;
	} u;
};

static __inline__ void eu_variant_init(struct eu_variant *var)
{
	var->metadata = NULL;
}

void eu_variant_fini(struct eu_variant *variant);

static __inline__ void eu_variant_assign_null(struct eu_variant *var)
{
	if (var->metadata)
		eu_variant_fini(var);

	var->metadata = &eu_null_metadata;
}

static __inline__ void eu_variant_assign_bool(struct eu_variant *var,
					    eu_bool_t val)
{
	if (var->metadata)
		eu_variant_fini(var);

	var->metadata = &eu_bool_metadata;
	var->u.bool = val;
}

static __inline__ void eu_variant_assign_number(struct eu_variant *var,
					      double val)
{
	if (var->metadata)
		eu_variant_fini(var);

	var->metadata = &eu_number_metadata;
	var->u.number = val;
}

static __inline__ int eu_variant_assign_string(struct eu_variant *var,
					       struct eu_string_ref s)
{
	if (var->metadata)
		eu_variant_fini(var);

	if (eu_string_init(&var->u.string, s)) {
		var->metadata = &eu_string_metadata;
		return 1;
	}
	else {
		var->metadata = NULL;
		return 0;
	}
}

struct eu_object *eu_variant_assign_object(struct eu_variant *var);

static __inline__ void eu_variant_array_init(struct eu_variant_array *array) {
	array->a = NULL;
	array->len = 0;
	array->priv.capacity = 0;
}

void eu_variant_array_fini(struct eu_variant_array *array);
struct eu_value eu_variant_array_value(struct eu_variant_array *array);

static __inline__ struct eu_variant *eu_variant_array_push(
					       struct eu_variant_array *array) {
	if (array->len < array->priv.capacity
	    || eu_array_grow((struct eu_array *)array, array->len + 1,
			     sizeof(struct eu_variant)))
		return array->a + array->len++;
	else
		return NULL;
}

extern const struct eu_metadata eu_variant_metadata;

static __inline__ struct eu_value eu_variant_value(struct eu_variant *variant)
{
	return eu_value(variant, &eu_variant_metadata);
}

enum eu_json_type eu_value_type(struct eu_value val);
void *eu_value_extract(struct eu_value val, enum eu_json_type type);

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

struct eu_struct_member_descriptor_v1 {
	unsigned int offset;
	unsigned short name_len;
	signed char presence_offset;
	unsigned char presence_bit;
	const char *name;
	const struct eu_type_descriptor *descriptor;
};

struct eu_struct_descriptor_v1 {
	struct eu_type_descriptor struct_base;
	struct eu_type_descriptor struct_ptr_base;
	unsigned int struct_size;
	unsigned int extras_offset;
	unsigned int extra_member_size;
	unsigned int extra_member_value_offset;
	size_t n_members;
	const struct eu_struct_member_descriptor_v1 *members;
	const struct eu_type_descriptor *extra_value_descriptor;
};

void eu_struct_extras_fini(const struct eu_metadata *md, void *v_extras);
size_t eu_object_size(struct eu_value val);

struct eu_object_iter {
	struct eu_string_ref name;
	struct eu_value value;
	struct eu_object_iter_priv *priv;
};

int eu_object_iter_init(struct eu_object_iter *iter, struct eu_value val);
int eu_object_iter_next(struct eu_object_iter *iter);
void eu_object_iter_fini(struct eu_object_iter *iter);

#endif
