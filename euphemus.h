#ifndef EUPHEMUS_EUPHEMUS_H
#define EUPHEMUS_EUPHEMUS_H

#include <stddef.h>
#include <stdlib.h>

struct eu_parse {
	struct eu_parse_cont *outer_stack;
	struct eu_parse_cont *stack_top;
	struct eu_parse_cont *stack_bottom;

	struct eu_metadata *metadata;
	void *result;

	const char *input;
	const char *input_end;

	char *member_name_buf;
	size_t member_name_len;
	size_t member_name_size;

	int error;
};

enum eu_parse_result {
	EU_PARSE_OK,
	EU_PARSE_PAUSED,
	EU_PARSE_ERROR
};

/* A continuation stack frame. */
struct eu_parse_cont {
	struct eu_parse_cont *next;
	enum eu_parse_result (*resume)(struct eu_parse *p,
				       struct eu_parse_cont *cont);
	void (*destroy)(struct eu_parse *ep, struct eu_parse_cont *cont);
};

enum eu_json_type {
	EU_JSON_INVALID,
	EU_JSON_STRING,
	EU_JSON_OBJECT,
	EU_JSON_MAX
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

	unsigned int size;
	unsigned char json_type;
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

/* Open structs */

struct eu_open_struct {
	struct eu_struct_extra *extras;
};

extern struct eu_struct_metadata eu_open_struct_metadata;

void eu_open_struct_fini(struct eu_open_struct *os);
struct eu_variant *eu_open_struct_get(struct eu_open_struct *os,
				      const char *name);

/* Strings */

struct eu_string {
	char *string;
	size_t len;
};

static __inline__ void eu_string_fini(struct eu_string *string)
{
	free(string->string);
}

extern struct eu_metadata eu_string_metadata;
void eu_parse_init_string(struct eu_parse *ep, struct eu_string *str);

/* Variants */

struct eu_variant {
	struct eu_metadata *metadata;
	union {
		struct eu_string string;
		struct eu_open_struct object;
	} u;
};

extern struct eu_metadata eu_variant_metadata;
void eu_parse_init_variant(struct eu_parse *ep, struct eu_variant *var);

static __inline__ enum eu_json_type eu_variant_type(struct eu_variant *variant)
{
	return variant->metadata->json_type;
}

struct eu_variant *eu_variant_get(struct eu_variant *variant, const char *name);

static __inline__ void eu_variant_fini(struct eu_variant *variant)
{
	if (variant->metadata)
		variant->metadata->fini(variant->metadata, &variant->u);
}

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
	unsigned int open_offset;
	int n_members;
	struct eu_struct_member *members;
};

struct eu_struct_extra {
	char *name;
	size_t name_len;
	struct eu_struct_extra *next;
	struct eu_variant value;
};

enum eu_parse_result eu_struct_parse(struct eu_metadata *gmetadata,
				     struct eu_parse *ep,
				     void *result);
void eu_struct_fini(struct eu_metadata *gmetadata, void *value);

enum eu_parse_result eu_inline_struct_parse(struct eu_metadata *gmetadata,
					    struct eu_parse *ep,
					    void *result);
void eu_inline_struct_fini(struct eu_metadata *gmetadata, void *value);

#define EU_STRUCT_METADATA_INITIALIZER(struct_name, struct_members)   \
	{                                                             \
		{                                                     \
			eu_struct_parse,                              \
			eu_struct_fini,                               \
			sizeof(struct_name *),                        \
			EU_JSON_INVALID                               \
		},                                                    \
		sizeof(struct_name),                                  \
		offsetof(struct foo, open),                           \
		sizeof(struct_members) / sizeof(struct eu_struct_member), \
		struct_members                                        \
	}

#define EU_INLINE_STRUCT_METADATA_INITIALIZER(struct_name, struct_members) \
	{                                                             \
		{                                                     \
			eu_inline_struct_parse,                       \
			eu_inline_struct_fini,                        \
			sizeof(struct_name),                          \
			EU_JSON_INVALID                               \
		},                                                    \
		-1,                                                   \
		offsetof(struct foo, open),                           \
		sizeof(struct_members) / sizeof(struct eu_struct_member), \
		struct_members                                        \
	}

#endif
