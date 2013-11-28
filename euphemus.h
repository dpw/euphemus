#ifndef EUPHEMUS_EUPHEMUS_H
#define EUPHEMUS_EUPHEMUS_H

#include <stdlib.h>

struct eu_parse {
	struct eu_parse_cont *outer_stack;
	struct eu_parse_cont *stack_top;
	struct eu_parse_cont *stack_bottom;

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
	void (*dispose)(struct eu_parse_cont *cont);
};

/* A description of a type of data (including things like how to
   allocate, release etc.) */
struct eu_metadata {
	struct eu_parse_cont base;

	/* A parse function expects that there is a non-whitespace
	   character available at the start of p->input.  So callers
	   need to skip any whitespace before calling the parse
	   function. */
	enum eu_parse_result (*parse)(struct eu_metadata *metadata,
				      struct eu_parse *ep,
				      void *result);
	void (*dispose)(struct eu_metadata *metadata, void *value);
};

void eu_parse_init(struct eu_parse *ep, struct eu_metadata *metadata,
		   void *result);
int eu_parse(struct eu_parse *ep, const char *input, size_t len);
int eu_parse_finish(struct eu_parse *ep);
void eu_parse_fini(struct eu_parse *ep);

enum eu_parse_result eu_parse_metadata_resume(struct eu_parse *ep,
					      struct eu_parse_cont *cont);
void eu_parse_cont_noop_dispose(struct eu_parse_cont *cont);

#define EU_METADATA_BASE_INITIALIZER                                  \
	{                                                             \
		NULL,                                                 \
		eu_parse_metadata_resume,                             \
		eu_parse_cont_noop_dispose                            \
	}

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
static struct eu_metadata *const eu_string_start = &eu_string_metadata;

/* Variants */

struct eu_variant {
	struct eu_metadata *metadata;
	union {
		struct eu_string string;
	} u;
};

extern struct eu_metadata eu_variant_metadata;
static struct eu_metadata *const eu_variant_start = &eu_variant_metadata;

static __inline__ void eu_variant_fini(struct eu_variant *variant)
{
	if (variant->metadata)
		variant->metadata->dispose(variant->metadata, &variant->u);
}

/* Structs */

struct struct_member {
	unsigned int offset;
	unsigned int name_len;
	const char *name;
	struct eu_metadata *metadata;
};

struct struct_metadata {
	struct eu_metadata base;
	unsigned int size;
	unsigned int extras_offset;
	int n_members;
	struct struct_member *members;
};

struct struct_extra {
	char *name;
	size_t name_len;
	struct struct_extra *next;
	struct eu_variant value;
};

enum eu_parse_result struct_parse(struct eu_metadata *gmetadata,
				  struct eu_parse *ep,
				  void *result);
void struct_dispose(struct eu_metadata *gmetadata, void *value);
void eu_struct_destroy_extras(struct struct_extra *extras);
struct eu_variant *eu_struct_get_extra(struct struct_extra *extras,
				       const char *name);

#define EU_STRUCT_METADATA_INITIALIZER(struct_name, struct_members)   \
	{                                                             \
		{                                                     \
			EU_METADATA_BASE_INITIALIZER,                 \
			struct_parse,                                 \
			struct_dispose                                \
		},                                                    \
		sizeof(struct_name),                                  \
		offsetof(struct foo, extras),                         \
		sizeof(struct_members) / sizeof(struct struct_member), \
		struct_members                                        \
	}

#endif
