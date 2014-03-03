#ifndef EUPHEMUS_EUPHEMUS_INT_H
#define EUPHEMUS_EUPHEMUS_INT_H

#include <stddef.h>
#include <string.h>
#include <stdint.h>

#ifdef __GNUC__
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define CACHE_ALIGN __attribute__ ((aligned (64)))
#define UNUSED __attribute__ ((unused))
#else
#define likely(x) (x)
#define unlikely(x) (x)
#define CACHE_ALIGN
#define UNUSED
#endif

#define PASTE(a,b) PASTE_AUX(a,b)
#define PASTE_AUX(a,b) a##b

#define STATIC_ASSERT(e) typedef char PASTE(_static_assert_,__LINE__)[1-2*!(e)] UNUSED

#define container_of(ptr, type, member) \
        ({ const __typeof__(((type *)0)->member ) *__mptr = (ptr); \
           ((type *)((char *)__mptr - offsetof(type,member))); })

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


struct eu_array_metadata {
	struct eu_metadata base;
	struct eu_metadata *element_metadata;
};

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



struct eu_introduce_chain {
	const struct eu_type_descriptor *descriptor;
	struct eu_metadata *metadata;
	struct eu_introduce_chain *next;
};

struct eu_metadata *eu_introduce_aux(const struct eu_type_descriptor *d,
				     struct eu_introduce_chain *chain_next);
struct eu_metadata *eu_introduce_struct(const struct eu_type_descriptor *d,
					struct eu_introduce_chain *c);
struct eu_metadata *eu_introduce_struct_ptr(const struct eu_type_descriptor *d,
					    struct eu_introduce_chain *c);

enum eu_parse_result {
	EU_PARSE_OK,
	EU_PARSE_PAUSED,
	EU_PARSE_ERROR,
	EU_PARSE_REINSTATE_PAUSED
};

struct eu_parse {
	char *stack;
	size_t scratch_size;
	size_t new_stack_bottom;
	size_t new_stack_top;
	size_t old_stack_bottom;
	size_t stack_area_size;

	struct eu_metadata *metadata;
	void *result;

	const char *input;
	const char *input_end;

	int error;
};

/* A continuation stack frame. */
struct eu_parse_cont {
	size_t size;
	enum eu_parse_result (*resume)(struct eu_parse *p,
				       struct eu_parse_cont *cont);
	void (*destroy)(struct eu_parse *ep, struct eu_parse_cont *cont);
};

void eu_parse_begin_pause(struct eu_parse *ep);
void *eu_parse_alloc_cont(struct eu_parse *ep, size_t size);
void *eu_parse_alloc_first_cont(struct eu_parse *ep, size_t size);
void eu_parse_cont_noop_destroy(struct eu_parse *ep, struct eu_parse_cont *cont);

int eu_parse_reserve_scratch(struct eu_parse *ep, size_t s);
void eu_parse_reset_scratch(struct eu_parse *ep);
int eu_parse_copy_to_scratch(struct eu_parse *ep, const char *start,
			     const char *end);
int eu_parse_append_to_scratch(struct eu_parse *ep, const char *start,
			       const char *end);
int eu_parse_append_to_scratch_with_nul(struct eu_parse *ep, const char *start,
					const char *end);

void eu_noop_fini(struct eu_metadata *metadata, void *value);
struct eu_value eu_get_fail(struct eu_value val, struct eu_string_ref name);

extern struct eu_struct_metadata eu_object_metadata;
extern struct eu_array_metadata eu_variant_array_metadata;
extern struct eu_metadata eu_null_metadata;
extern struct eu_bool_misc eu_bool_true;
extern struct eu_bool_misc eu_bool_false;

enum eu_parse_result eu_variant_string(void *string_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result);
enum eu_parse_result eu_variant_object(void *object_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result);
enum eu_parse_result eu_variant_array(void *array_metadata,
				      struct eu_parse *ep,
				      struct eu_variant *result);
enum eu_parse_result eu_variant_number(void *number_metadata,
				       struct eu_parse *ep,
				       struct eu_variant *result);
enum eu_parse_result eu_variant_bool(void *misc, struct eu_parse *ep,
				     struct eu_variant *result);
enum eu_parse_result eu_variant_n(void *null_metadata, struct eu_parse *ep,
				  struct eu_variant *result);

#define WHITESPACE_CASES ' ': case '\t': case '\n': case '\r'

static __inline__ const char *skip_whitespace(const char *p, const char *end)
{
	/* Not doing UTF-8 yet, so no error or pause returns */

	for (; p != end; p++) {
		switch (*p) {
		case WHITESPACE_CASES:
			break;

		default:
			goto out;
		}
	}

 out:
	return p;
}

enum eu_parse_result eu_consume_whitespace_pause(struct eu_metadata *metadata,
						 struct eu_parse *ep,
						 void *result);

static __inline__ enum eu_parse_result eu_consume_whitespace(
						struct eu_metadata *metadata,
						struct eu_parse *ep,
						void *result)
{
	const char *end = ep->input_end;
	ep->input = skip_whitespace(ep->input, end);
	if (ep->input != end)
		return EU_PARSE_OK;
	else
		return eu_consume_whitespace_pause(metadata, ep, result);
}

enum eu_parse_result eu_consume_ws_until_slow(struct eu_metadata *metadata,
					      struct eu_parse *ep,
					      void *result,
					      char c);

static __inline__ enum eu_parse_result eu_consume_whitespace_until(
						struct eu_metadata *metadata,
						struct eu_parse *ep,
						void *result,
						char c)
{
	if (unlikely(*ep->input != c)) {
		enum eu_parse_result res
			= eu_consume_ws_until_slow(metadata, ep, result, c);
		if (unlikely(res != EU_PARSE_OK))
			return res;
	}

	return EU_PARSE_OK;
}

enum eu_parse_result eu_parse_expect_slow(struct eu_parse *ep,
					  const char *expect,
					  unsigned int expect_len);

#if !(defined(__i386__) || defined(__x86_64__))

struct expect {
	unsigned int len;
	const char *str;
};

#define EXPECT_INIT(l, lit, str) {                                    \
	(l),                                                          \
	str                                                           \
}

#define EXPECT_ASSIGN(e, l, lit, s) do {                              \
	(e).len = (l);                                                \
	(e).str = s;                                                  \
} while (0);

static __inline__ enum eu_parse_result eu_parse_expect(struct eu_parse *ep,
						       struct expect e)
{
	if ((size_t)(ep->input_end - ep->input) >= e.len) {
		if (likely(!memcmp(ep->input, e.str, e.len))) {
			ep->input += e.len;
			return EU_PARSE_OK;
		}

		return EU_PARSE_ERROR;
	}
	else {
		return eu_parse_expect_slow(ep, e.str, e.len);
	}
}

#else

/* This variant avoids a memcmp in the fast path, but relies on
   support for unaliagned loads and assumes little-endian. */

struct expect {
	uint32_t mask;
	uint32_t val;
	unsigned int len;
	const char *str;
};

#define EXPECT_INIT(l, lit, str) {                                    \
	(l) < 4 ? ((uint32_t)1 << ((l & 3)*8)) - 1 : 0xffffffff,      \
	__builtin_bswap32(lit) >> (4 - (l)) * 8,                      \
	(l),                                                          \
	str                                                           \
}

#define EXPECT_ASSIGN(e, l, lit, s) do {                              \
	(e).mask = ((uint32_t)1 << ((l)*8)) - 1;                      \
	(e).val = __builtin_bswap32(lit) >> (4 - (l)) * 8;            \
	(e).len = (l);                                                \
	(e).str = s;                                                  \
} while (0);

static __inline__ enum eu_parse_result eu_parse_expect(struct eu_parse *ep,
						       struct expect e)
{
	if (ep->input_end - ep->input >= 4) {
		if (likely((*(uint32_t *)ep->input & e.mask) == e.val)) {
			ep->input += e.len;
			return EU_PARSE_OK;
		}

		return EU_PARSE_ERROR;
	}
	else {
		return eu_parse_expect_slow(ep, e.str, e.len);
	}
}

#endif

#endif
