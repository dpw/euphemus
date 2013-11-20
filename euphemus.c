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
				      struct eu_parse *p,
				      void *result);
	void (*dispose)(struct eu_metadata *metadata, void *value);
};


void eu_parse_init(struct eu_parse *p, struct eu_metadata *metadata,
		   void *result)
{
	p->outer_stack = &metadata->base;
	p->stack_top = p->stack_bottom = NULL;
	p->result = result;
	p->member_name_buf = NULL;
	p->member_name_size = 0;
	p->error = 0;
}

void eu_parse_fini(struct eu_parse *p)
{
	struct eu_parse_cont *c, *next;

	/* If the parse was unfinished, there might be stack frames to
	   clean up. */
	for (c = p->outer_stack; c; c = next) {
		next = c->next;
		c->dispose(c);
	}

	for (c = p->stack_top; c; c = next) {
		next = c->next;
		c->dispose(c);
	}

	free(p->member_name_buf);
}

static void set_only_cont(struct eu_parse *p, struct eu_parse_cont *c)
{
	assert(!p->outer_stack);
	p->outer_stack = c;
}

static void noop_cont_dispose(struct eu_parse_cont *cont)
{
	(void)cont;
}

static void insert_cont(struct eu_parse *p, struct eu_parse_cont *c)
{
	c->next = NULL;
	if (p->stack_bottom) {
		p->stack_bottom->next = c;
		p->stack_bottom = c;
	}
	else {
		p->stack_top = p->stack_bottom = c;
	}
}

static int set_member_name_buf(struct eu_parse *p, const char *start,
			       const char *end)
{
	size_t len = end - start;

	if (len > p->member_name_size) {
		free(p->member_name_buf);
		if (!(p->member_name_buf = malloc(len)))
			return 0;

		p->member_name_size = len;
	}

	memcpy(p->member_name_buf, start, len);
	p->member_name_len = len;
	return 1;
}

static int append_member_name_buf(struct eu_parse *p, const char *start,
				  const char *end)
{
	size_t len = end - start;
	size_t total_len = p->member_name_len + len;

	if (total_len > p->member_name_size) {
		char *buf = malloc(total_len);
		if (!buf)
			return 0;

		p->member_name_size = total_len;
		memcpy(buf, p->member_name_buf, p->member_name_len);
		free(p->member_name_buf);
		p->member_name_buf = buf;
	}

	memcpy(p->member_name_buf + p->member_name_len, start, len);
	p->member_name_len = total_len;
	return 1;
}

static void skip_whitespace(struct eu_parse *p)
{
	/* Not doing UTF-8 yet, so no error or pause returns */

	for (; p->input != p->input_end; p->input++) {
		switch (*p->input) {
		case ' ':
		case '\f':
		case '\n':
		case '\r':
		case '\t':
		case '\v':
			break;

		default:
			return;
		}
	}
}

static enum eu_parse_result metadata_cont_func(struct eu_parse *p,
					       struct eu_parse_cont *cont)
{
	struct eu_metadata *metadata = (struct eu_metadata *)cont;

	skip_whitespace(p);

	if (p->input != p->input_end) {
		return metadata->parse(metadata, p, p->result);
	}
	else {
		set_only_cont(p, cont);
		return EU_PARSE_PAUSED;
	}
}

int eu_parse(struct eu_parse *p, const char *input, size_t len)
{
	enum eu_parse_result res;

	if (p->error)
		return 0;

	/* Need to concatenate the inner and outer stacks */
	if (p->stack_top) {
		p->stack_bottom->next = p->outer_stack;
		p->outer_stack = p->stack_top;
		p->stack_top = p->stack_bottom = NULL;
	}

	p->input = input;
	p->input_end = input + len;

	for (;;) {
		struct eu_parse_cont *s = p->outer_stack;
		if (!s)
			break;

		p->outer_stack = s->next;
		res = s->resume(p, s);
		if (res == EU_PARSE_OK)
			continue;

		if (res == EU_PARSE_PAUSED)
			return 1;
		else
			goto error;

	}

	skip_whitespace(p);
	if (p->input == p->input_end)
		return 1;

 error:
	p->error = 1;
	return 0;
}

void *eu_parse_finish(struct eu_parse *p)
{
	if (p->error || p->outer_stack || p->stack_top)
		return NULL;

	return p->result;
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

static enum eu_parse_result struct_parse_resume(struct eu_parse *p,
						struct eu_parse_cont *gcont);
static void struct_parse_cont_dispose(struct eu_parse_cont *cont);

/* This parses, allocating a fresh struct. */
static enum eu_parse_result struct_parse(struct eu_metadata *gmetadata,
					 struct eu_parse *p,
					 void *result)
{
	struct struct_parse_cont *cont;
	struct struct_metadata *metadata = (struct struct_metadata *)gmetadata;
	enum struct_parse_state state;
	char *s = malloc(metadata->size);
	struct struct_member *member;

	enum eu_parse_result res;
	const char *i;

	if (!s)
		goto alloc_error;

	memset(s, 0, metadata->size);
	*(void **)result = s;

	/* Read the opening brace */
	if (*p->input != '{')
		goto error;

	p->input++;

#define STRUCT_PARSE_BODY                                             \
	state = STRUCT_PARSE_OPEN;                                    \
RESUME_ONLY(case STRUCT_PARSE_OPEN:)                                  \
	skip_whitespace(p);                                           \
	if (p->input == p->input_end)                                 \
		goto pause;                                           \
                                                                      \
	switch (*p->input) {                                          \
	case '\"':                                                    \
		break;                                                \
                                                                      \
	case '}':                                                     \
		p->input++;                                           \
		goto done;                                            \
                                                                      \
	default:                                                      \
		goto error;                                           \
	}                                                             \
                                                                      \
	for (;;) {                                                    \
		for (i = ++p->input;; i++) {                          \
			if (i == p->input_end)                        \
				goto pause_in_member_name;            \
                                                                      \
			if (*i == '\"')                               \
				break;                                \
		}                                                     \
                                                                      \
		member = lookup_member(metadata, p->input, i);        \
RESUME_ONLY(looked_up_member:)                                        \
		if (!member)                                          \
			goto error;                                   \
                                                                      \
		p->input = i + 1;                                     \
		state = STRUCT_PARSE_MEMBER_NAME;                     \
RESUME_ONLY(case STRUCT_PARSE_MEMBER_NAME:)                           \
		skip_whitespace(p);                                   \
		if (p->input == p->input_end)                         \
			goto pause;                                   \
                                                                      \
		if (*p->input != ':')                                 \
			goto error;                                   \
                                                                      \
		p->input++;                                           \
		state = STRUCT_PARSE_COLON;                           \
RESUME_ONLY(case STRUCT_PARSE_COLON:)                                 \
		skip_whitespace(p);                                   \
		if (p->input == p->input_end)                         \
			goto pause;                                   \
                                                                      \
		state = STRUCT_PARSE_MEMBER_VALUE;                    \
		res = member->metadata->parse(member->metadata, p,    \
		                              s + member->offset);    \
		switch (res) {                                        \
		case EU_PARSE_OK:                                     \
			break;                                        \
                                                                      \
		case EU_PARSE_PAUSED:                                 \
			goto pause;                                   \
                                                                      \
		default:                                              \
			goto error;                                   \
		}                                                     \
                                                                      \
RESUME_ONLY(case STRUCT_PARSE_MEMBER_VALUE:)                          \
		skip_whitespace(p);                                   \
		if (p->input == p->input_end)                         \
			goto pause;                                   \
                                                                      \
		switch (*p->input) {                                  \
		case ',':                                             \
			p->input++;                                   \
			break;                                        \
                                                                      \
		case '}':                                             \
			p->input++;                                   \
			goto done;                                    \
                                                                      \
		default:                                              \
			goto error;                                   \
		}                                                     \
                                                                      \
		state = STRUCT_PARSE_COMMA;                           \
RESUME_ONLY(case STRUCT_PARSE_COMMA:)                                 \
		skip_whitespace(p);                                   \
		if (p->input == p->input_end)                         \
			goto pause;                                   \
                                                                      \
		if (*p->input != '\"')                                \
			goto error;                                   \
	}                                                             \
                                                                      \
 done:                                                                \
	return EU_PARSE_OK;                                           \
                                                                      \
 pause_in_member_name:                                                \
	state = STRUCT_PARSE_IN_MEMBER_NAME;                          \
	if (!set_member_name_buf(p, p->input, i))                     \
		goto alloc_error;                                     \
                                                                      \
 pause:                                                               \
	cont = malloc(sizeof *cont);                                  \
	cont->base.resume = struct_parse_resume;                      \
	cont->base.dispose = struct_parse_cont_dispose;               \
	cont->state = state;                                          \
	cont->metadata = metadata;                                    \
	cont->s = s;                                                  \
	cont->member = member;                                        \
	insert_cont(p, (struct eu_parse_cont *)cont);                 \
	return EU_PARSE_PAUSED;                                       \
                                                                      \
 alloc_error:                                                         \
 error:                                                               \
	return EU_PARSE_ERROR;

#define RESUME_ONLY(x)
	STRUCT_PARSE_BODY
#undef RESUME_ONLY
}

/* This parses, allocating a fresh struct. */
static enum eu_parse_result struct_parse_resume(struct eu_parse *p,
						struct eu_parse_cont *gcont)
{
	struct struct_parse_cont *cont = (struct struct_parse_cont *)gcont;
	enum struct_parse_state state = cont->state;
	struct struct_metadata *metadata = cont->metadata;
	char *s = cont->s;
	struct struct_member *member = cont->member;

	enum eu_parse_result res;
	const char *i;

	free(cont);

#define RESUME_ONLY(x) x
	switch (state) {
	STRUCT_PARSE_BODY

	case STRUCT_PARSE_IN_MEMBER_NAME:
		/* The member name was split, so we can't simply
		   resume in this case. */
		for (i = p->input;; i++) {
			if (i == p->input_end) {
				if (!append_member_name_buf(p, p->input, i))
					goto alloc_error;

				goto pause;
			}

			if (*i == '\"')
				break;
		}

		member = lookup_member_2(metadata, p->member_name_buf,
					 p->member_name_len, p->input, i);
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

struct foo {
	struct foo *bar;
	struct foo *baz;
};

static struct struct_metadata foo_metadata;

static struct struct_member foo_members[] = {
	{
		offsetof(struct foo, bar),
		3,
		"bar",
		&foo_metadata.base
	},
	{
		offsetof(struct foo, baz),
		3,
		"baz",
		&foo_metadata.base
	}
};

static struct struct_metadata foo_metadata = {
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

static struct eu_metadata *const foo_start = &foo_metadata.base;

void foo_destroy(struct foo *foo)
{
	if (foo->bar)
		foo_destroy(foo->bar);

	if (foo->baz)
		foo_destroy(foo->baz);

	free(foo);
}

int main(void)
{
	struct eu_parse p;
	const char data[] = "  {  \"bar\"  :  {  }  , \"baz\"  : {  } }  ";
	size_t len = strlen(data);
	struct foo *foo;
	size_t i;

	for (i = 0; i < len; i++) {
		eu_parse_init(&p, foo_start, &foo);
		assert(eu_parse(&p, data, i));
		assert(eu_parse(&p, data + i, len - i));
		eu_parse_finish(&p);
		eu_parse_fini(&p);

		assert(foo);
		assert(foo->bar);
		assert(!foo->bar->bar);
		foo_destroy(foo);
	}

	eu_parse_init(&p, foo_start, &foo);
	for (i = 0; i < len; i++)
		assert(eu_parse(&p, data + i, 1));

	eu_parse_finish(&p);
	eu_parse_fini(&p);

	assert(foo);
	assert(foo->bar);
	assert(!foo->bar->bar);
	foo_destroy(foo);

	/* Test that resources are released after an unfinished parse. */
	eu_parse_init(&p, foo_start, &foo);
	eu_parse_fini(&p);

	eu_parse_init(&p, foo_start, &foo);
	assert(eu_parse(&p, data, len / 2));
	eu_parse_fini(&p);

	return 0;
}
