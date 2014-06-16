#ifndef EUPHEMUS_EUPHEMUS_INT_H
#define EUPHEMUS_EUPHEMUS_INT_H

#include <stddef.h>
#include <string.h>
#include <locale.h>

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
	enum eu_result (*parse)(const struct eu_metadata *metadata,
				struct eu_parse *ep, void *result);

	enum eu_result (*generate)(const struct eu_metadata *metadata,
				   struct eu_generate *eg, void *value);

	/* Release any resources associated with the value.*/
	void (*fini)(const struct eu_metadata *metadata, void *value);

	/* Get a member of an object or array. */
	struct eu_value (*get)(struct eu_value val, struct eu_string_ref name);

	int (*object_iter_init)(struct eu_value val,
				struct eu_object_iter *iter);
	size_t (*object_size)(struct eu_value val);

	/* Get the value of a number */
	struct eu_maybe_double (*to_double)(struct eu_value val);
	struct eu_maybe_integer (*to_integer)(struct eu_value val);
};

struct eu_introduce_chain {
	const struct eu_type_descriptor *descriptor;
	struct eu_metadata *metadata;
	struct eu_introduce_chain *next;
};

const struct eu_metadata *eu_introduce_aux(const struct eu_type_descriptor *d,
					 struct eu_introduce_chain *chain_next);
const struct eu_metadata *eu_introduce_struct(const struct eu_type_descriptor *d,
					      struct eu_introduce_chain *c);
const struct eu_metadata *eu_introduce_struct_ptr(
					      const struct eu_type_descriptor *d,
					      struct eu_introduce_chain *c);
const struct eu_metadata *eu_introduce_array(const struct eu_type_descriptor *gd,
					     struct eu_introduce_chain *chain);

struct eu_object_iter_priv {
	int (*next)(struct eu_object_iter *iter);
};

/* Parse/Generation stack management */

struct eu_stack {
	char *stack;
	size_t scratch_size;
	size_t new_stack_bottom;
	size_t new_stack_top;
	size_t old_stack_bottom;
	size_t stack_area_size;
};

/* Result codes for parsing a generation */
enum eu_result {
	EU_OK,
	EU_PAUSED,
	EU_ERROR,
	EU_REINSTATE_PAUSED
};

/* A stack frame. */
struct eu_stack_frame {
	size_t size;
	enum eu_result (*resume)(struct eu_stack_frame *frame,
				 void *context);
	void (*destroy)(struct eu_stack_frame *frame);
};


void *eu_stack_init(struct eu_stack *st, size_t alloc_size);
void eu_stack_fini(struct eu_stack *st);
void eu_stack_begin_pause(struct eu_stack *st);
void *eu_stack_alloc(struct eu_stack *st, size_t size);

static __inline__ void *eu_stack_alloc_first(struct eu_stack *st, size_t size)
{
	eu_stack_begin_pause(st);
	return eu_stack_alloc(st, size);
}

enum eu_result eu_stack_run(struct eu_stack *st, void *context);

static __inline__ int eu_stack_empty(struct eu_stack *st)
{
	return st->new_stack_bottom == st->new_stack_top
		&& st->old_stack_bottom == st->stack_area_size;
}

int eu_stack_reserve_scratch(struct eu_stack *st, size_t s);

static __inline__ int eu_stack_reserve_more_scratch(struct eu_stack *st,
						    size_t s)
{
	return eu_stack_reserve_scratch(st, st->scratch_size + s);
}

static __inline__ void eu_stack_reset_scratch(struct eu_stack *st)
{
	st->scratch_size = 0;
}

static __inline__ void eu_stack_set_scratch_end(struct eu_stack *st,
						char *end)
{
	st->scratch_size = end - st->stack;
}

static __inline__ char *eu_stack_scratch(struct eu_stack *st)
{
	return st->stack;
}

static __inline__ char *eu_stack_scratch_end(struct eu_stack *st)
{
	return st->stack + st->scratch_size;
}

static __inline__ struct eu_string_ref eu_stack_scratch_ref(struct eu_stack *st)
{
	return eu_string_ref(st->stack, st->scratch_size);
}

int eu_stack_set_scratch(struct eu_stack *st, const char *start,
			 const char *end);
int eu_stack_append_scratch(struct eu_stack *st, const char *start,
			    const char *end);
int eu_stack_append_scratch_with_nul(struct eu_stack *st, const char *start,
				     const char *end);

void eu_stack_frame_noop_destroy(struct eu_stack_frame *frame);

/* Locale ugliness */

struct eu_locale {
	locale_t c_locale;
	locale_t old_locale;
};

static __inline__ void eu_locale_init(struct eu_locale *el)
{
	el->c_locale = el->old_locale = 0;
}

static __inline__ void eu_locale_fini(struct eu_locale *el)
{
	if (el->c_locale)
		freelocale(el->c_locale);
}

static __inline__ int eu_locale_c(struct eu_locale *el)
{
	if (el->old_locale) {
		return 1;
	}
	else {
		if (!el->c_locale) {
			el->c_locale = newlocale(LC_ALL, "C", (locale_t)0);
			if (!el->c_locale)
				return 0;
		}

		el->old_locale = uselocale(el->c_locale);
		return !!el->old_locale;
	}
}

static __inline__ void eu_locale_restore(struct eu_locale *el)
{
	if (el->old_locale) {
		uselocale(el->old_locale);
		el->old_locale = 0;
	}
}

/* JSON parsing */

struct eu_parse {
	struct eu_stack stack;
	const struct eu_metadata *metadata;
	void *result;

	const char *input;
	const char *input_end;

	struct eu_locale locale;
	int error;
};

void eu_noop_fini(const struct eu_metadata *metadata, void *value);
struct eu_value eu_get_fail(struct eu_value val, struct eu_string_ref name);
int eu_object_iter_init_fail(struct eu_value val, struct eu_object_iter *iter);
size_t eu_object_size_fail(struct eu_value val);
enum eu_result eu_generate_fail(const struct eu_metadata *metadata,
				struct eu_generate *eg, void *value);
struct eu_maybe_double eu_to_double_fail(struct eu_value val);
struct eu_maybe_integer eu_to_integer_fail(struct eu_value val);

extern const struct eu_metadata eu_null_metadata;
extern const struct eu_bool_misc eu_bool_true;
extern const struct eu_bool_misc eu_bool_false;

enum eu_result eu_variant_string(const void *string_metadata,
				 struct eu_parse *ep,
				 struct eu_variant *result);
enum eu_result eu_variant_object(const void *object_metadata,
				 struct eu_parse *ep,
				 struct eu_variant *result);
enum eu_result eu_variant_array(const void *array_metadata,
				struct eu_parse *ep,
				struct eu_variant *result);
enum eu_result eu_variant_number(const void *number_metadata,
				 struct eu_parse *ep,
				 struct eu_variant *result);
enum eu_result eu_variant_bool(const void *misc, struct eu_parse *ep,
			       struct eu_variant *result);
enum eu_result eu_variant_n(const void *null_metadata, struct eu_parse *ep,
			    struct eu_variant *result);

/* The JSON spec only allows ASCII whitespace chars */
#define WHITESPACE_CASES ' ': case '\t': case '\n': case '\r'

static __inline__ const char *skip_whitespace(const char *p, const char *end)
{
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

enum eu_result eu_consume_whitespace_pause(const struct eu_metadata *metadata,
					   struct eu_parse *ep,
					   void *result);

static __inline__ enum eu_result eu_consume_whitespace(
					const struct eu_metadata *metadata,
					struct eu_parse *ep,
					void *result)
{
	const char *end = ep->input_end;
	ep->input = skip_whitespace(ep->input, end);
	if (ep->input != end)
		return EU_OK;
	else
		return eu_consume_whitespace_pause(metadata, ep, result);
}

enum eu_result eu_consume_ws_until_slow(const struct eu_metadata *metadata,
					struct eu_parse *ep, void *result,
					char c);

static __inline__ enum eu_result eu_consume_whitespace_until(
					const struct eu_metadata *metadata,
					struct eu_parse *ep,
					void *result,
					char c)
{
	if (unlikely(*ep->input != c)) {
		enum eu_result res
			= eu_consume_ws_until_slow(metadata, ep, result, c);
		if (unlikely(res != EU_OK))
			return res;
	}

	return EU_OK;
}

enum eu_result eu_parse_expect_slow(struct eu_parse *ep, const char *expect,
				    unsigned int expect_len);

/* JSON generation */

struct eu_generate {
	struct eu_stack stack;

	char *output;
	char *output_end;

	struct eu_locale locale;
	eu_bool_t error;
};

enum eu_result eu_fixed_gen_slow(struct eu_generate *eg, const char *str,
				 unsigned int len);

#if !(defined(__i386__) || defined(__x86_64__))
/*#if 1*/

#define MULTICHAR_2(a, b) UNUSED_MULTICHAR
#define MULTICHAR_3(a, b, c) UNUSED_MULTICHAR
#define MULTICHAR_4(a, b, c, d) UNUSED_MULTICHAR
#define MULTICHAR_5(a, b, c, d, e) UNUSED_MULTICHAR

struct expect {
	unsigned int len;
	const char *str;
};

#define EXPECT_INIT(l, chars, str) { (l), str }

#define EXPECT_ASSIGN(e, l, chars, s) do {                              \
	(e).len = (l);                                                \
	(e).str = s;                                                  \
} while (0);

static __inline__ enum eu_result eu_parse_expect(struct eu_parse *ep,
						 struct expect e)
{
	if ((size_t)(ep->input_end - ep->input) >= e.len) {
		if (likely(!memcmp(ep->input, e.str, e.len))) {
			ep->input += e.len;
			return EU_OK;
		}

		return EU_ERROR;
	}
	else {
		return eu_parse_expect_slow(ep, e.str, e.len);
	}
}

/* eu_fixed_gen_32 is defined as a macro in order to discard the
   multichar parameter without compile-time evaluation. */
#define eu_fixed_gen_32(eg, len, chars, str) eu_fixed_gen_slow(eg, str, len)

struct fixed_gen_64 {
	unsigned int len;
	const char *str;
};

#define FIXED_GEN_64_INIT(l, chars, str) { (l), str }

static __inline__ enum eu_result eu_fixed_gen_64(struct eu_generate *eg,
						 struct fixed_gen_64 fg)
{
	return eu_fixed_gen_slow(eg, fg.str, fg.len);
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

/* Little-endian multichar constants */
#define MULTICHAR_2(a, b) (a | ((uint32_t)b << 8))
#define MULTICHAR_3(a, b, c) (MULTICHAR_2(a, b) | ((uint32_t)c << 16))
#define MULTICHAR_4(a, b, c, d) (MULTICHAR_3(a, b, c) | ((uint32_t)d << 24))
#define MULTICHAR_5(a, b, c, d, e) (MULTICHAR_4(a, b, c, d) | ((uint64_t)e << 32))

#define EXPECT_INIT(l, chars, str) {                                  \
	(l) < 4 ? ((uint32_t)1 << ((l & 3)*8)) - 1 : 0xffffffff,      \
	(chars),                                                      \
	(l),                                                          \
	str                                                           \
}

#define EXPECT_ASSIGN(e, l, chars, s) do {                            \
	(e).mask = ((uint32_t)1 << ((l)*8)) - 1;                      \
	(e).val = (chars);                                            \
	(e).len = (l);                                                \
	(e).str = s;                                                  \
} while (0);

static __inline__ enum eu_result eu_parse_expect(struct eu_parse *ep,
						 struct expect e)
{
	if (ep->input_end - ep->input >= 4) {
		if (likely((*(uint32_t *)ep->input & e.mask) == e.val)) {
			ep->input += e.len;
			return EU_OK;
		}

		return EU_ERROR;
	}
	else {
		return eu_parse_expect_slow(ep, e.str, e.len);
	}
}

static __inline__ enum eu_result eu_fixed_gen_32(struct eu_generate *eg,
						 unsigned int len,
						 uint32_t chars,
						 const char *str)
{
	if (eg->output_end - eg->output >= 4) {
		*(uint32_t *)eg->output = chars;
		eg->output += len;
		return EU_OK;
	}
	else {
		return eu_fixed_gen_slow(eg, str, len);
	}
}

struct fixed_gen_64 {
	uint64_t chars;
	unsigned int len;
	const char *str;
};

#define FIXED_GEN_64_INIT(l, chars, str) { (chars), (l), str }

static __inline__ enum eu_result eu_fixed_gen_64(struct eu_generate *eg,
						 struct fixed_gen_64 fg)
{
	if (eg->output_end - eg->output >= 8) {
		*(uint64_t *)eg->output = fg.chars;
		eg->output += fg.len;
		return EU_OK;
	}
	else {
		return eu_fixed_gen_slow(eg, fg.str, fg.len);
	}
}

#endif

/* Generate an escaped string.  Does not generate the leading '"'
   char, but does generate the terminating '"'.  And does not require
   the caller to ensure a byte of space in the output buffer. */
enum eu_result eu_escape(struct eu_generate *eg, struct eu_string_ref str);

#endif
