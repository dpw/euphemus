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
	p->result = NULL;
	p->error = 0;
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


typedef enum eu_parse_result (*eu_parse_func_t)(struct eu_parse *p,
						void *metadata,
						void *result);

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

/* This parses, allocating a fresh struct. */
static enum eu_parse_result struct_parse(struct eu_parse *p, void *v_metadata,
					 void *result)
{
	struct struct_metadata *metadata = v_metadata;
	char *s;
	const char *i;
	struct struct_member *member;
	enum eu_parse_result res;

	/* Read the opening brace */
	skip_whitespace(p);
	if (p->input == p->input_end)
		goto open_brace_pause;

	if (*p->input != '{')
		goto error;

	p->input++;

	/* Allocate the struct, based on size info in the metadata */
	s = malloc(metadata->size);
	if (!s)
		goto alloc_error;

	memset(s, 0, metadata->size);

	/* Scan the first member name */
	skip_whitespace(p);
	if (p->input == p->input_end)
		goto pause;

	switch (*p->input) {
	case '\"':
		break;

	case '}':
		p->input++;
		goto done;

	default:
		goto error;
	}

	for (;;) {
		i = ++p->input;

		/* Lots missing here */
		for (;;) {
			if (i == p->input_end)
				goto pause;

			if (*i == '\"')
				break;

			i++;
		}

		member = lookup_member(metadata, p->input, i);
		if (!member)
			goto error;

		p->input = i + 1;
		skip_whitespace(p);
		if (p->input == p->input_end)
			goto pause;

		if (*p->input != ':')
			goto error;

		p->input++;
		res = member->parse(p, member->metadata, s + member->offset);
		if (res != EU_PARSE_OK)
			goto error;

		skip_whitespace(p);
		if (p->input == p->input_end)
			goto pause;

		switch (*p->input) {
		case ',':
			p->input++;
			break;

		case '}':
			p->input++;
			goto done;

		default:
			goto error;
		}

		/* Scan the next member name */
		skip_whitespace(p);
		if (p->input == p->input_end)
			goto pause;

		if (*p->input != '\"')
			goto error;
	}

 done:
	*(void **)result = s;
	return EU_PARSE_OK;

 open_brace_pause:
	if (insert_simple_cont(p, struct_parse, metadata, result))
		return EU_PARSE_PAUSED;
	else
		return EU_PARSE_ERROR;

 pause:
	return EU_PARSE_PAUSED;

 alloc_error:
 error:
	return EU_PARSE_ERROR;
}


struct foo {
	struct foo *bar;
};

static struct struct_metadata foo_metadata;

static struct struct_member foo_members[] = {
	{
		offsetof(struct foo, bar),
		3,
		"bar",
		struct_parse,
		&foo_metadata
	}
};

static struct struct_metadata foo_metadata = {
	sizeof(struct foo),
	1,
	foo_members
};

enum eu_parse_result foo_start_func(struct eu_parse *p,
				    struct eu_parse_cont *cont)
{
	(void)cont;
	return struct_parse(p, &foo_metadata, &p->result);
}

struct eu_parse_cont foo_start[1] = {{
	NULL,
	foo_start_func
}};

void foo_destroy(struct foo *foo)
{
	if (foo->bar)
		foo_destroy(foo->bar);

	free(foo);
}

int main(void)
{
	struct eu_parse p;
	const char data[] = " { \"bar\" : { } } ";
	struct foo *foo;

	eu_parse_init(&p, foo_start);
	assert(eu_parse(&p, data, strlen(data)));
	foo = eu_parse_finish(&p);
	assert(foo);
	assert(foo->bar);
	assert(!foo->bar->bar);
	foo_destroy(foo);

	eu_parse_init(&p, foo_start);
	assert(eu_parse(&p, " ", 1));
	assert(eu_parse(&p, data, strlen(data)));
	foo = eu_parse_finish(&p);
	assert(foo);
	assert(foo->bar);
	assert(!foo->bar->bar);
	foo_destroy(foo);

	return 0;
}
