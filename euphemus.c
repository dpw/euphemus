#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

enum eu_parse_result {
	EU_PARSE_OK,
	EU_PARSE_PAUSED,
	EU_PARSE_ERROR
};

struct eu_parse {
	const char *input;
	const char *input_end;
	struct eu_parse_cont *outer_stack;
	struct eu_parse_cont *stack_top;
	struct eu_parse_cont *stack_bottom;

	char *member_name_buf;
	size_t member_name_len;
	size_t member_name_size;

	void *result;
	int error;
};


typedef enum eu_parse_result (*eu_cont_func_t)(struct eu_parse *p,
					       struct eu_parse_cont *cont);

struct eu_parse_cont {
	struct eu_parse_cont *next;
	eu_cont_func_t func;
};



static const char dummy_input[1] = { 0 };

void eu_parse_init(struct eu_parse *p, struct eu_parse_cont *s)
{
	p->input = p->input_end = dummy_input;
	p->outer_stack = s;
	p->stack_top = p->stack_bottom = NULL;
	p->member_name_buf = NULL;
	p->member_name_size = 0;
	p->error = 0;
}

void eu_parse_fini(struct eu_parse *p)
{
	/* XXX need to call destructor function on stack frames */

	free(p->member_name_buf);
}

static void set_only_cont(struct eu_parse *p, struct eu_parse_cont *c)
{
	assert(!p->outer_stack);
	p->outer_stack = c;
}

/* A parse function expects that there is a non-whitespace character
   available at the start of p->input.  So callers need to skip any
   whitespace before calling the parse function. */
typedef enum eu_parse_result (*eu_parse_func_t)(struct eu_parse *p,
						void *metadata,
						void *result);

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

#if 0
struct simple_cont {
	struct eu_parse_cont cont;
	eu_parse_func_t parse;
	void *metadata;
	void *result;
};

static enum eu_parse_result simple_cont_func(struct eu_parse *p,
					     struct eu_parse_cont *gc)
{
	struct simple_cont *c = (struct simple_cont *)gc;
	eu_parse_func_t parse = c->parse;
	void *metadata = c->metadata;
	void *result = c->result;

	free(c);
	return parse(p, metadata, result);
}

static int insert_simple_cont(struct eu_parse *p, eu_parse_func_t parse,
			      void *metadata, void *result)
{
	struct simple_cont *c = malloc(sizeof *c);
	if (!c)
		return 0;

	c->cont.func = simple_cont_func;
	c->parse = parse;
	c->metadata = metadata;
	c->result = result;
	insert_cont(p, (struct eu_parse_cont *)c);
	return 1;
}
#endif

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
		res = s->func(p, s);
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
	else
		return p->result;
}

struct struct_member {
	unsigned int offset;
	unsigned int name_len;
	const char *name;
	eu_parse_func_t parse;
	void *metadata;
};

struct struct_metadata {
	unsigned int size;
	unsigned int n_members;
	struct struct_member *members;
};

static struct struct_member *lookup_member(struct struct_metadata *md,
					   const char *name,
					   const char *name_end)
{
	unsigned int name_len = name_end - name;
	unsigned int i;

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
	unsigned int i;

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
	struct eu_parse_cont cont;
	enum struct_parse_state state;
	struct struct_metadata *metadata;
	char *s;
	struct struct_member *member;
};

static enum eu_parse_result struct_parse_restart(struct eu_parse *p,
						 struct eu_parse_cont *gcont);


/* This parses, allocating a fresh struct. */
static enum eu_parse_result struct_parse(struct eu_parse *p, void *v_metadata,
					 void *result)
{
	struct struct_parse_cont *cont;
	struct struct_metadata *metadata = v_metadata;
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
RESTART_ONLY(case STRUCT_PARSE_OPEN:)                                 \
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
RESTART_ONLY(looked_up_member:)                                       \
		if (!member)                                          \
			goto error;                                   \
                                                                      \
		p->input = i + 1;                                     \
		state = STRUCT_PARSE_MEMBER_NAME;                     \
RESTART_ONLY(case STRUCT_PARSE_MEMBER_NAME:)                          \
		skip_whitespace(p);                                   \
		if (p->input == p->input_end)                         \
			goto pause;                                   \
                                                                      \
		if (*p->input != ':')                                 \
			goto error;                                   \
                                                                      \
		p->input++;                                           \
		state = STRUCT_PARSE_COLON;                           \
RESTART_ONLY(case STRUCT_PARSE_COLON:)                                \
		skip_whitespace(p);                                   \
		if (p->input == p->input_end)                         \
			goto pause;                                   \
                                                                      \
		state = STRUCT_PARSE_MEMBER_VALUE;                    \
		res = member->parse(p, member->metadata, s + member->offset); \
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
RESTART_ONLY(case STRUCT_PARSE_MEMBER_VALUE:)                         \
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
RESTART_ONLY(case STRUCT_PARSE_COMMA:)                                \
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
	cont->cont.func = struct_parse_restart;                       \
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

#define RESTART_ONLY(x)
	STRUCT_PARSE_BODY
#undef RESTART_ONLY
}

/* This parses, allocating a fresh struct. */
static enum eu_parse_result struct_parse_restart(struct eu_parse *p,
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

#define RESTART_ONLY(x) x
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
#undef RESTART_ONLY
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
		struct_parse,
		&foo_metadata
	},
	{
		offsetof(struct foo, baz),
		3,
		"baz",
		struct_parse,
		&foo_metadata
	}
};

static struct struct_metadata foo_metadata = {
	sizeof(struct foo),
	2,
	foo_members
};

static enum eu_parse_result foo_start_func(struct eu_parse *p,
					   struct eu_parse_cont *cont);

struct eu_parse_cont foo_start[1] = {{
	NULL,
	foo_start_func
}};

static enum eu_parse_result foo_start_func(struct eu_parse *p,
					   struct eu_parse_cont *cont)
{
	(void)cont;

	skip_whitespace(p);

	if (p->input != p->input_end)
		return struct_parse(p, &foo_metadata, &p->result);

	set_only_cont(p, foo_start);
	return EU_PARSE_PAUSED;
}


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
	struct foo *foo;
	size_t len = strlen(data);
	size_t i;

	for (i = 0; i < len; i++) {
		eu_parse_init(&p, foo_start);
		assert(eu_parse(&p, data, i));
		assert(eu_parse(&p, data + i, len - i));
		foo = eu_parse_finish(&p);
		eu_parse_fini(&p);
		assert(foo);
		assert(foo->bar);
		assert(!foo->bar->bar);
		foo_destroy(foo);
	}

	eu_parse_init(&p, foo_start);
	for (i = 0; i < len; i++)
		assert(eu_parse(&p, data + i, 1));

	foo = eu_parse_finish(&p);
	eu_parse_fini(&p);
	assert(foo);
	assert(foo->bar);
	assert(!foo->bar->bar);
	foo_destroy(foo);

	return 0;
}
