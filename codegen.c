#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "euphemus.h"

static void die(const char *fmt, ...)
	__attribute__ ((noreturn,format (printf, 1, 2)));

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

static void *xalloc(size_t s)
{
        void *res = malloc(s);
        if (res)
                return res;

        die("malloc(%ld) failed", (long)s);
}

static size_t xsprintf(char **buf, const char *fmt, ...)
{
	int res;
	va_list ap;
	va_start(ap, fmt);

	res = vasprintf(buf, fmt, ap);
	va_end(ap);
	if (res >= 0)
		return res;

	die("asprintf failed");
}

static void parse_schema_file(const char *path, struct eu_variant *var)
{
	struct eu_parse parse;
	FILE *fp = fopen(path, "r");

	if (!fp)
		die("error opening \"%s\": %s", path, strerror(errno));

	eu_parse_init_variant(&parse, var);

	while (!feof(fp)) {
		char buf[1000];
		size_t res = fread(buf, 1, 1000, fp);

		if (res < 1000 && ferror(fp))
			die("error reading \"%s\": %s", path, strerror(errno));

		if (!eu_parse(&parse, buf, res))
			die("parse error in \"%s\"", path);
	}

	if (!eu_parse_finish(&parse))
		die("parse error in \"%s\"", path);

	fclose(fp);

	eu_parse_fini(&parse);
}

static int eu_variant_equals_cstr(struct eu_variant *var, const char *str)
{
	size_t len = strlen(str);
	assert(eu_variant_type(var) == EU_JSON_STRING);
	return len == var->u.string.len
		&& !memcmp(str, var->u.string.chars, len);
}

struct type_info {
	struct type_info_ops *ops;
	char *metadata_ptr_expr;
	struct type_info *next_to_destroy;
};

struct type_info_ops {
	void (*declare)(struct type_info *ti, FILE *out,
			const char *name, size_t name_len);
	void (*define)(struct type_info *ti, FILE *out);
	void (*call_fini)(struct type_info *ti, FILE *out,
			  const char *var_expr);
	void (*destroy)(struct type_info *ti);
};

static void noop_define(struct type_info *ti, FILE *out)
{
	(void)ti;
	(void)out;
}

/* Declare a variable or field of the given type. */
static void declare(struct type_info *ti, FILE *out,
		    const char *name, size_t name_len)
{
	ti->ops->declare(ti, out, name, name_len);
}

/* Define the type */
static void define_type(struct type_info *ti, FILE *out)
{
	ti->ops->define(ti, out);
}

/* Generate a finalizer call for the type */
void call_fini(struct type_info *ti, FILE *out, const char *var_expr)
{
	ti->ops->call_fini(ti, out, var_expr);
}


/* The doegen object holds everything for code generation */

struct codegen {
	struct type_info *type_infos_to_destroy;
};


static void codegen_init(struct codegen *codegen)
{
	codegen->type_infos_to_destroy = NULL;
}

static void codegen_fini(struct codegen *codegen)
{
	struct type_info *ti, *next;

	for (ti = codegen->type_infos_to_destroy; ti;) {
		next = ti->next_to_destroy;
		ti->ops->destroy(ti);
		ti = next;
	}
}

static struct type_info *resolve(struct codegen *codegen,
				 struct eu_variant *schema);

static void add_type_info_to_destroy(struct codegen *codegen,
				     struct type_info *ti)
{
	ti->next_to_destroy = codegen->type_infos_to_destroy;
	codegen->type_infos_to_destroy = ti;
}


/* String, numbers, etc. */

struct simple_type_info {
	struct type_info base;
	const char *type_name;
	const char *fini_func;
};

static void simple_type_declare(struct type_info *ti, FILE *out,
				const char *name, size_t name_len)
{
	struct simple_type_info *sti = (void *)ti;

	fprintf(out, "\t%s %.*s;\n", sti->type_name, (int)name_len, name);
}

static void simple_type_call_fini(struct type_info *ti, FILE *out,
				  const char *var_expr)
{
	struct simple_type_info *sti = (void *)ti;

	fprintf(out, "\t%s(&%s);\n", sti->fini_func, var_expr);
}

struct type_info_ops simple_type_info_ops = {
	simple_type_declare,
	noop_define,
	simple_type_call_fini,
	NULL
};

#define DEFINE_SIMPLE_TYPE_INFO(name, type_name, metadata_ptr_expr, fini_func) \
struct  simple_type_info name = {                                     \
	{                                                             \
		&simple_type_info_ops,                                \
		metadata_ptr_expr,                                    \
		NULL                                                  \
	},                                                            \
	type_name,                                                    \
	fini_func                                                     \
}

DEFINE_SIMPLE_TYPE_INFO(string_type_info, "struct eu_string",
			"&eu_string_metadata",
			"eu_string_fini");
DEFINE_SIMPLE_TYPE_INFO(number_type_info, "double",
			"&eu_number_metadata",
			"(void)");

/* Structs */

struct member_info {
	const char *name;
	size_t name_len;
	struct type_info *type_info;
};

struct struct_type_info {
	struct type_info base;

	const char *struct_name;
	size_t struct_name_len;

	char *metadata_name;
	char *inline_metadata_name;

	struct member_info *members;
	size_t members_len;
};

static struct type_info_ops struct_type_info_ops;

static struct type_info *make_struct(struct eu_variant *schema,
				     struct codegen *codegen)
{
	struct struct_type_info *sti = xalloc(sizeof *sti);
	struct eu_variant *name = eu_variant_get(schema, "euphemusStructName");
	struct eu_variant *props = eu_variant_get(schema, "properties");
	struct eu_variant_members *props_members;
	size_t i;

	assert(name && eu_variant_type(name) == EU_JSON_STRING);
	assert(props && eu_variant_type(props) == EU_JSON_OBJECT);

	sti->base.ops = &struct_type_info_ops;
	sti->struct_name = name->u.string.chars;
	sti->struct_name_len = name->u.string.len;

	xsprintf(&sti->metadata_name, "struct_%.*s_metadata",
		 (int)sti->struct_name_len, sti->struct_name);
	xsprintf(&sti->inline_metadata_name, "inline_struct_%.*s_metadata",
		 (int)sti->struct_name_len, sti->struct_name);
	xsprintf(&sti->base.metadata_ptr_expr, "&%s.base", sti->metadata_name);

	props_members = &props->u.object.members;
	sti->members = xalloc(props_members->len * sizeof *sti->members);
	sti->members_len = props_members->len;

	for (i = 0; i < props_members->len; i++) {
		struct eu_variant_member *sm = &props_members->members[i];
		struct member_info *mi = &sti->members[i];

		mi->name = sm->name;
		mi->name_len = sm->name_len;
		mi->type_info = resolve(codegen, &sm->value);
	}

	add_type_info_to_destroy(codegen, &sti->base);

	return &sti->base;
}

static void struct_declare(struct type_info *ti, FILE *out,
			   const char *name, size_t name_len)
{
	struct struct_type_info *sti = (void *)ti;

	fprintf(out, "\tstruct %.*s *%.*s;\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)name_len, name);
}

static void struct_define(struct type_info *ti, FILE *out)
{
	struct struct_type_info *sti = (void *)ti;
	size_t i;

	/* The defintions of any types used in this struct. */
	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		define_type(mi->type_info, out);
	}

	/* The definition of the struct itself. */
	fprintf(out, "struct %.*s {\n",
		(int)sti->struct_name_len, sti->struct_name);

	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		declare(mi->type_info, out, mi->name, mi->name_len);
	}

	fprintf(out,
		"\tstruct eu_variant_members extras;\n"
		"};\n\n");

	/* Member metadata */
	fprintf(out,
		"static struct eu_struct_member struct_%.*s_members[%d] = {\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->members_len);

	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];

		/* TODO need to escape member name below */
		fprintf(out,
			"\t{\n"
			"\t\toffsetof(struct %.*s, %.*s),\n"
			"\t\t%d,\n"
			"\t\t\"%.*s\",\n"
			"\t\t%s\n"
			"\t},\n",
			(int)sti->struct_name_len, sti->struct_name,
			(int)mi->name_len, mi->name,
			(int)mi->name_len,
			(int)mi->name_len, mi->name,
			mi->type_info->metadata_ptr_expr);
	}

	fprintf(out, "};\n\n");

	/* Definitiion of the eu_struct_metadata instance */
	fprintf(out,
		"struct eu_struct_metadata %s\n"
		"\t= EU_STRUCT_METADATA_INITIALIZER(struct %.*s, "
			"struct_%.*s_members);\n\n",
		sti->metadata_name,
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	/* Definitiion of the eu_struct_metadata instance for inline
	   structs.  Inline support is incomplete currently. */
	fprintf(out,
		"struct eu_struct_metadata %s\n"
		"\t= EU_INLINE_STRUCT_METADATA_INITIALIZER(struct %.*s, "
			"struct_%.*s_members);\n\n",
		sti->inline_metadata_name,
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	fprintf(out,
		"void %.*s_fini(struct %.*s *p)\n"
		"{\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		char *var;

		xsprintf(&var, "p->%.*s", (int)mi->name_len, mi->name);
		call_fini(mi->type_info, out, var);
		free(var);
	}

	fprintf(out, "\teu_variant_members_fini(&p->extras);\n");

	fprintf(out, "}\n\n");

	fprintf(out,
		"void %.*s_destroy(struct %.*s *p)\n"
		"{\n"
		"\t%.*s_fini(p);\n"
		"\tfree(p);\n"
		"}\n\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);
}

static void struct_destroy(struct type_info *ti)
{
	struct struct_type_info *sti = (void *)ti;

	free(sti->metadata_name);
	free(sti->inline_metadata_name);
	free(sti->base.metadata_ptr_expr);
	free(sti->members);
	free(sti);
}

static void struct_generate_fini(struct type_info *ti, FILE *out,
				 const char *var_expr)
{
	struct struct_type_info *sti = (void *)ti;

	fprintf(out, "\tif (%s) %.*s_destroy(%s);\n",
		var_expr,
		(int)sti->struct_name_len, sti->struct_name,
		var_expr);
}

static struct type_info_ops struct_type_info_ops = {
	struct_declare,
	struct_define,
	struct_generate_fini,
	struct_destroy
};


static struct type_info *resolve(struct codegen *codegen,
				 struct eu_variant *schema)
{
	struct eu_variant *type;

	assert(eu_variant_type(schema) == EU_JSON_OBJECT);

	type = eu_variant_get(schema, "type");
	assert(type && eu_variant_type(type) == EU_JSON_STRING);

	if (eu_variant_equals_cstr(type, "string"))
		return &string_type_info.base;
	else if (eu_variant_equals_cstr(type, "number"))
		return &number_type_info.base;
	else if (eu_variant_equals_cstr(type, "object"))
		return make_struct(schema, codegen);
	else
		die("unknown type \"%.*s\"", (int)type->u.string.len,
		    type->u.string.chars);
}

static void codegen(FILE *out, struct eu_variant *schema)
{
	struct codegen codegen;

	fprintf(out, "#include \"euphemus.h\"\n\n");

	codegen_init(&codegen);
	define_type(resolve(&codegen, schema), out);
	codegen_fini(&codegen);
}

int main(int argc, char **argv)
{
	int i;
	struct eu_variant var;

	for (i = 1; i < argc; i++) {
		parse_schema_file(argv[i], &var);
		codegen(stdout, &var);
		eu_variant_fini(&var);
	}

	return 0;
}
