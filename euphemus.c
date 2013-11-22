#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

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
		   void *result)
{
	ep->outer_stack = &metadata->base;
	ep->stack_top = ep->stack_bottom = NULL;
	ep->result = result;
	ep->member_name_buf = NULL;
	ep->member_name_size = 0;
	ep->error = 0;
}

void eu_parse_fini(struct eu_parse *ep)
{
	struct eu_parse_cont *c, *next;

	/* If the parse was unfinished, there might be stack frames to
	   clean up. */
	for (c = ep->outer_stack; c; c = next) {
		next = c->next;
		c->dispose(c);
	}

	for (c = ep->stack_top; c; c = next) {
		next = c->next;
		c->dispose(c);
	}

	free(ep->member_name_buf);
}

static void set_only_cont(struct eu_parse *ep, struct eu_parse_cont *c)
{
	assert(!ep->outer_stack);
	ep->outer_stack = c;
}

static void noop_cont_dispose(struct eu_parse_cont *cont)
{
	(void)cont;
}

static void insert_cont(struct eu_parse *ep, struct eu_parse_cont *c)
{
	c->next = NULL;
	if (ep->stack_bottom) {
		ep->stack_bottom->next = c;
		ep->stack_bottom = c;
	}
	else {
		ep->stack_top = ep->stack_bottom = c;
	}
}

static int set_member_name_buf(struct eu_parse *ep, const char *start,
			       const char *end)
{
	size_t len = end - start;

	if (len > ep->member_name_size) {
		free(ep->member_name_buf);
		if (!(ep->member_name_buf = malloc(len)))
			return 0;

		ep->member_name_size = len;
	}

	memcpy(ep->member_name_buf, start, len);
	ep->member_name_len = len;
	return 1;
}

static int append_member_name_buf(struct eu_parse *ep, const char *start,
				  const char *end)
{
	size_t len = end - start;
	size_t total_len = ep->member_name_len + len;

	if (total_len > ep->member_name_size) {
		char *buf = malloc(total_len);
		if (!buf)
			return 0;

		ep->member_name_size = total_len;
		memcpy(buf, ep->member_name_buf, ep->member_name_len);
		free(ep->member_name_buf);
		ep->member_name_buf = buf;
	}

	memcpy(ep->member_name_buf + ep->member_name_len, start, len);
	ep->member_name_len = total_len;
	return 1;
}

static const char *skip_whitespace(const char *p, const char *end)
{
	/* Not doing UTF-8 yet, so no error or pause returns */

	for (; p != end; p++) {
		switch (*p) {
		case ' ':
		case '\f':
		case '\n':
		case '\r':
		case '\t':
		case '\v':
			break;

		default:
			goto out;
		}
	}

 out:
	return p;
}

static enum eu_parse_result metadata_cont_func(struct eu_parse *ep,
					       struct eu_parse_cont *cont)
{
	struct eu_metadata *metadata = (struct eu_metadata *)cont;

	ep->input = skip_whitespace(ep->input, ep->input_end);

	if (ep->input != ep->input_end) {
		return metadata->parse(metadata, ep, ep->result);
	}
	else {
		set_only_cont(ep, cont);
		return EU_PARSE_PAUSED;
	}
}

int eu_parse(struct eu_parse *ep, const char *input, size_t len)
{
	enum eu_parse_result res;

	if (ep->error)
		return 0;

	/* Need to concatenate the inner and outer stacks */
	if (ep->stack_top) {
		ep->stack_bottom->next = ep->outer_stack;
		ep->outer_stack = ep->stack_top;
		ep->stack_top = ep->stack_bottom = NULL;
	}

	ep->input = input;
	ep->input_end = input + len;

	for (;;) {
		struct eu_parse_cont *s = ep->outer_stack;
		if (!s)
			break;

		ep->outer_stack = s->next;
		res = s->resume(ep, s);
		if (res == EU_PARSE_OK)
			continue;

		if (res == EU_PARSE_PAUSED)
			return 1;
		else
			goto error;

	}

	ep->input = skip_whitespace(ep->input, ep->input_end);
	if (ep->input == ep->input_end)
		return 1;

 error:
	ep->error = 1;
	return 0;
}

int eu_parse_finish(struct eu_parse *ep)
{
	return !ep->error && !ep->outer_stack && !ep->stack_top;
}

struct struct_member {
	unsigned int offset;
	unsigned int name_len;
	const char *name;
	struct eu_metadata *metadata;
};

struct struct_metadata {
	struct eu_metadata base;
	unsigned int size;
	int n_members;
	struct struct_member *members;
};

static struct struct_member *lookup_member(struct struct_metadata *md,
					   const char *name,
					   const char *name_end)
{
	unsigned int name_len = name_end - name;
	int i;

	for (i = 0; i < md->n_members; i++) {
		struct struct_member *m = &md->members[i];
		if (m->name_len != name_len)
			continue;

		if (!memcmp(m->name, name, name_len))
			return m;
	}

	return NULL;
}

static struct struct_member *lookup_member_2(struct struct_metadata *md,
					     const char *buf,
					     size_t buf_len,
					     const char *more,
					     const char *more_end)
{
	size_t more_len = more_end - more;
	size_t name_len = buf_len + more_len;
	int i;

	for (i = 0; i < md->n_members; i++) {
		struct struct_member *m = &md->members[i];
		if (m->name_len != name_len)
			continue;

		if (!memcmp(m->name, buf, buf_len)
		    && !memcmp(m->name + buf_len, more, more_len))
			return m;
	}

	return NULL;
}

/* A state name refers to the token the precedes it (except for
   *_IN_*). */
enum struct_parse_state {
	STRUCT_PARSE_OPEN,
	STRUCT_PARSE_IN_MEMBER_NAME,
	STRUCT_PARSE_MEMBER_NAME,
	STRUCT_PARSE_COLON,
	STRUCT_PARSE_MEMBER_VALUE,
	STRUCT_PARSE_COMMA
};

struct struct_parse_cont {
	struct eu_parse_cont base;
	enum struct_parse_state state;
	struct struct_metadata *metadata;
	char *s;
	struct struct_member *member;
};

static enum eu_parse_result struct_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont);
static void struct_parse_cont_dispose(struct eu_parse_cont *cont);

/* This parses, allocating a fresh struct. */
static enum eu_parse_result struct_parse(struct eu_metadata *gmetadata,
					 struct eu_parse *ep,
					 void *result)
{
	struct struct_parse_cont *cont;
	struct struct_metadata *metadata = (struct struct_metadata *)gmetadata;
	enum struct_parse_state state;
	char *s = malloc(metadata->size);
	struct struct_member *member;
	enum eu_parse_result res;
	const char *p = ep->input;
	const char *end = ep->input_end;

	if (!s)
		goto alloc_error;

	memset(s, 0, metadata->size);
	*(void **)result = s;

	if (*p != '{')
		goto error;

	p++;

#define STRUCT_PARSE_BODY                                             \
	state = STRUCT_PARSE_OPEN;                                    \
RESUME_ONLY(case STRUCT_PARSE_OPEN:)                                  \
	p = skip_whitespace(p, end);				      \
	if (p == end)                                                 \
		goto pause;                                           \
                                                                      \
	switch (*p) {                                                 \
	case '\"':                                                    \
		break;                                                \
                                                                      \
	case '}':                                                     \
		goto done;                                            \
                                                                      \
	default:                                                      \
		goto error;                                           \
	}                                                             \
                                                                      \
	for (;;) {                                                    \
		/* Record the start of the member name */             \
		ep->input = ++p;                                      \
		for (;; p++) {                                        \
			if (p == end)                                 \
				goto pause_in_member_name;            \
                                                                      \
			if (*p == '\"')                               \
				break;                                \
		}                                                     \
                                                                      \
		member = lookup_member(metadata, ep->input, p);       \
RESUME_ONLY(looked_up_member:)                                        \
		if (!member)                                          \
			goto error;                                   \
                                                                      \
		p++;                                                  \
		state = STRUCT_PARSE_MEMBER_NAME;                     \
RESUME_ONLY(case STRUCT_PARSE_MEMBER_NAME:)                           \
		p = skip_whitespace(p, end);                          \
		if (p == end)                                         \
			goto pause;                                   \
                                                                      \
		if (*p != ':')                                        \
			goto error;                                   \
                                                                      \
		p++;                                                  \
		state = STRUCT_PARSE_COLON;                           \
RESUME_ONLY(case STRUCT_PARSE_COLON:)                                 \
		p = skip_whitespace(p, end);                          \
		if (p == end)                                         \
			goto pause;                                   \
                                                                      \
		state = STRUCT_PARSE_MEMBER_VALUE;                    \
		ep->input = p;                                        \
		res = member->metadata->parse(member->metadata, ep,   \
		                              s + member->offset);    \
		switch (res) {                                        \
		case EU_PARSE_OK:                                     \
			break;                                        \
                                                                      \
		case EU_PARSE_PAUSED:                                 \
			goto pause_input_set;                         \
                                                                      \
		default:                                              \
			goto error_input_set;                         \
		}                                                     \
                                                                      \
		end = ep->input_end;                                  \
RESUME_ONLY(case STRUCT_PARSE_MEMBER_VALUE:)                          \
		p = skip_whitespace(ep->input, end);                  \
		if (p == end)                                         \
			goto pause;                                   \
                                                                      \
		switch (*p) {                                         \
		case ',':                                             \
			break;                                        \
                                                                      \
		case '}':                                             \
			goto done;                                    \
                                                                      \
		default:                                              \
			goto error;                                   \
		}                                                     \
                                                                      \
		p++;                                                  \
		state = STRUCT_PARSE_COMMA;                           \
RESUME_ONLY(case STRUCT_PARSE_COMMA:)                                 \
		p = skip_whitespace(p, end);                          \
		if (p == end)                                         \
			goto pause;                                   \
                                                                      \
		if (*p != '\"')                                       \
			goto error;                                   \
	}                                                             \
                                                                      \
 done:                                                                \
	ep->input = p + 1;                                            \
	return EU_PARSE_OK;                                           \
                                                                      \
 pause_in_member_name:                                                \
	state = STRUCT_PARSE_IN_MEMBER_NAME;                          \
	if (!set_member_name_buf(ep, ep->input, p))                   \
		goto alloc_error;                                     \
                                                                      \
 pause:                                                               \
	ep->input = p;                                                \
 pause_input_set:                                                     \
	cont = malloc(sizeof *cont);                                  \
	if (!cont)                                                    \
		goto alloc_error;                                     \
                                                                      \
	cont->base.resume = struct_parse_resume;                      \
	cont->base.dispose = struct_parse_cont_dispose;               \
	cont->state = state;                                          \
	cont->metadata = metadata;                                    \
	cont->s = s;                                                  \
	cont->member = member;                                        \
	insert_cont(ep, &cont->base);                                 \
	return EU_PARSE_PAUSED;                                       \
                                                                      \
 alloc_error:                                                         \
 error:                                                               \
	ep->input = p;                                                \
 error_input_set:                                                     \
	return EU_PARSE_ERROR;

#define RESUME_ONLY(x)
	STRUCT_PARSE_BODY
#undef RESUME_ONLY
}

static enum eu_parse_result struct_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont)
{
	struct struct_parse_cont *cont = (struct struct_parse_cont *)gcont;
	enum struct_parse_state state = cont->state;
	struct struct_metadata *metadata = cont->metadata;
	char *s = cont->s;
	struct struct_member *member = cont->member;
	enum eu_parse_result res;
	const char *p = ep->input;
	const char *end = ep->input_end;

	free(cont);

#define RESUME_ONLY(x) x
	switch (state) {
	STRUCT_PARSE_BODY

	case STRUCT_PARSE_IN_MEMBER_NAME:
		/* The member name was split, so we can't simply
		   resume in this case. */
		for (;; p++) {
			if (p == end) {
				if (!append_member_name_buf(ep, ep->input, p))
					goto alloc_error;

				goto pause;
			}

			if (*p == '\"')
				break;
		}

		member = lookup_member_2(metadata, ep->member_name_buf,
					 ep->member_name_len, ep->input, p);
		goto looked_up_member;

	default:
		goto error;
	}
#undef RESUME_ONLY
}

static void struct_free(struct struct_metadata *metadata, char *s)
{
	int i;

	if (!s)
		return;

	for (i = 0; i < metadata->n_members; i++) {
		struct struct_member *member = &metadata->members[i];
		member->metadata->dispose(member->metadata, s + member->offset);
	}

	free(s);
}

void struct_dispose(struct eu_metadata *gmetadata, void *value)
{
	struct_free((struct struct_metadata *)gmetadata,
		    *(void **)value);
}

static void struct_parse_cont_dispose(struct eu_parse_cont *gcont)
{
	struct struct_parse_cont *cont = (struct struct_parse_cont *)gcont;
	struct_free(cont->metadata, cont->s);
	free(cont);
}



struct eu_string {
	char *string;
	size_t len;
};

void eu_string_fini(struct eu_string *string)
{
	free(string->string);
}

struct string_parse_cont {
	struct eu_parse_cont base;
	struct eu_string *result;
	char *buf;
	size_t len;
	size_t capacity;
};

static enum eu_parse_result string_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont);
static void string_parse_cont_dispose(struct eu_parse_cont *cont);

static enum eu_parse_result string_parse(struct eu_metadata *metadata,
					 struct eu_parse *ep,
					 void *v_result)
{
	const char *p = ep->input;
	const char *end = ep->input_end;
	struct eu_string *result = v_result;
	struct string_parse_cont *cont;

	(void)metadata;

	if (*p != '\"')
		goto error;

	ep->input = ++p;

	for (;; p++) {
		if (p == end)
			goto pause;

		if (*p == '\"')
			break;
	}

	result->len = p - ep->input;
	if (!(result->string = malloc(result->len)))
		goto alloc_error;

	memcpy(result->string, ep->input, result->len);
	ep->input = p + 1;
	return EU_PARSE_OK;

 pause:
	cont = malloc(sizeof *cont);
	if (!cont)
		goto alloc_error;

	cont->base.resume = string_parse_resume;
	cont->base.dispose = string_parse_cont_dispose;
	cont->result = result;
	cont->len = p - ep->input;
	cont->capacity = cont->len * 2;
	cont->buf = malloc(cont->capacity);
	if (cont->buf) {
		memcpy(cont->buf, ep->input, cont->len);
		ep->input = p;
		insert_cont(ep, &cont->base);
		return EU_PARSE_PAUSED;
	}

	free(cont);

 alloc_error:
 error:
	ep->input = p;
	return EU_PARSE_ERROR;
}

static enum eu_parse_result string_parse_resume(struct eu_parse *ep,
						struct eu_parse_cont *gcont)
{
	struct string_parse_cont *cont = (struct string_parse_cont *)gcont;
	const char *p = ep->input;
	const char *end = ep->input_end;
	char *buf;
	size_t len, total_len;

	for (;; p++) {
		if (p == end)
			goto pause;

		if (*p == '\"')
			break;
	}

	len = p - ep->input;
	total_len = cont->len + len;
	if (total_len <= cont->capacity) {
		buf = cont->buf;
	}
	else {
		buf = realloc(cont->buf, total_len);
		if (!buf)
			goto alloc_error;
	}

	memcpy(buf + cont->len, ep->input, len);
	cont->result->string = buf;
	cont->result->len = total_len;
	ep->input = p + 1;
	free(cont);
	return EU_PARSE_OK;

 pause:
	len = p - ep->input;
	total_len = cont->len + len;
	if (total_len > cont->capacity) {
		size_t new_capacity = total_len * 2;
		buf = realloc(cont->buf, new_capacity);
		if (!buf)
			goto alloc_error;

		cont->buf = buf;
		cont->capacity = new_capacity;
	}

	memcpy(cont->buf + cont->len, ep->input, len);
	cont->len = total_len;
	ep->input = p + 1;
	insert_cont(ep, &cont->base);
	return EU_PARSE_PAUSED;

 alloc_error:
	free(cont->buf);
	free(cont);
	return EU_PARSE_ERROR;
}

static void string_parse_cont_dispose(struct eu_parse_cont *gcont)
{
	struct string_parse_cont *cont = (struct string_parse_cont *)gcont;

	free(cont->buf);
	free(cont);
}

static void string_dispose(struct eu_metadata *metadata, void *value)
{
	struct eu_string *str = value;

	(void)metadata;

	free(str->string);
}


struct eu_metadata eu_string_metadata = {
	{
		NULL,
		metadata_cont_func,
		noop_cont_dispose
	},
	string_parse,
	string_dispose
};

static struct eu_metadata *const eu_string_start = &eu_string_metadata;



struct foo {
	struct foo *bar;
	struct foo *baz;
};

static struct struct_metadata struct_foo_metadata;

static struct struct_member foo_members[] = {
	{
		offsetof(struct foo, bar),
		3,
		"bar",
		&struct_foo_metadata.base
	},
	{
		offsetof(struct foo, baz),
		3,
		"baz",
		&struct_foo_metadata.base
	}
};

static struct struct_metadata struct_foo_metadata = {
	{
		{
			NULL,
			metadata_cont_func,
			noop_cont_dispose
		},
		struct_parse,
		struct_dispose
	},
	sizeof(struct foo),
	2,
	foo_members
};

static struct eu_metadata *const foo_start = &struct_foo_metadata.base;

void foo_destroy(struct foo *foo)
{
	if (foo->bar)
		foo_destroy(foo->bar);

	if (foo->baz)
		foo_destroy(foo->baz);

	free(foo);
}

static void test_parse(const char *json, struct eu_metadata *start,
		       void *result, void (*validate)(void *result))
{
	struct eu_parse ep;
	size_t len = strlen(json);
	size_t i;

	/* Test parsing in one go */
	eu_parse_init(&ep, start, result);
	assert(eu_parse(&ep, json, len));
	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);
	validate(result);

	/* Test parsing broken at each position within the json */
	for (i = 0; i < len; i++) {
		eu_parse_init(&ep, start, result);
		assert(eu_parse(&ep, json, i));
		assert(eu_parse(&ep, json + i, len - i));
		assert(eu_parse_finish(&ep));
		eu_parse_fini(&ep);
		validate(result);
	}

	/* Test parsing with the json broken into individual bytes */
	eu_parse_init(&ep, start, result);
	for (i = 0; i < len; i++)
		assert(eu_parse(&ep, json + i, 1));

	assert(eu_parse_finish(&ep));
	eu_parse_fini(&ep);
	validate(result);

	/* Test that resources are released after an unfinished parse. */
	eu_parse_init(&ep, start, result);
	eu_parse_fini(&ep);

	eu_parse_init(&ep, start, result);
	assert(eu_parse(&ep, json, len / 2));
	eu_parse_fini(&ep);
}

static void validate_foo(void *v_foo)
{
	struct foo *foo = *(struct foo **)v_foo;

	assert(foo);
	assert(foo->bar);
	assert(!foo->bar->bar);
	assert(!foo->bar->baz);
	assert(foo->baz);
	assert(!foo->baz->bar);
	assert(!foo->baz->baz);
	foo_destroy(foo);
}

static void test_struct(void)
{
	struct foo *foo;

	test_parse("  {  \"bar\"  :  {  }  , \"baz\"  : {  } }  ", foo_start,
		   &foo, validate_foo);
}

static void validate_string(void *v_string)
{
	struct eu_string *string = v_string;

	assert(string->len == 13);
	assert(!memcmp(string->string, "hello, world!", 13));
	eu_string_fini(string);
}

void test_string(void)
{
	struct eu_string string;

	test_parse("  \"hello, world!\"  ", eu_string_start,
		   &string, validate_string);
}

int main(void)
{
	test_string();
	test_struct();
	return 0;
}
