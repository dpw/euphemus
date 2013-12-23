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

static char *xsprintf(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

static char *xsprintf(const char *fmt, ...)
{
	int res;
	char *buf;
	va_list ap;
	va_start(ap, fmt);

	res = vasprintf(&buf, fmt, ap);
	va_end(ap);
	if (res >= 0)
		return buf;

	die("asprintf failed");
}

static char *xstrdup(const char *s)
{
	char *res = strdup(s);
	if (res)
		return res;

	die("strdup failed");
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

/* The codegen object holds everything for code generation */

struct definition {
	struct eu_string_value name;
	enum {
		DEF_SCHEMA,
		DEF_REF,
		DEF_RESOLVING,
		DEF_SCHEMA_TYPE,
		DEF_TYPE
	} state;
	union {
		struct eu_variant *schema;
		struct definition *ref;
		struct type_info *type;
	} u;
};

struct codegen {
	struct type_info *type_infos_to_destroy;
	struct definition *defs;
	size_t n_defs;
};

struct type_info {
	struct type_info_ops *ops;
	char *metadata_ptr_expr;
	struct type_info *next_to_destroy;
};

struct type_info_ops {
	void (*fill)(struct type_info *ti, struct codegen *codegen,
		     struct eu_variant *schema);
	void (*declare)(struct type_info *ti, FILE *out,
			const char *name, size_t name_len);
	void (*define)(struct type_info *ti, FILE *c_out, FILE *h_out);
	void (*call_fini)(struct type_info *ti, FILE *out,
			  const char *var_expr);
	void (*destroy)(struct type_info *ti);
};

/* Fill in sub-schema information. */
static void fill_type(struct type_info *ti, struct codegen *codegen,
		      struct eu_variant *schema)
{
	ti->ops->fill(ti, codegen, schema);
}

/* Declare a variable or field of the given type. */
static void declare(struct type_info *ti, FILE *out,
		    const char *name, size_t name_len)
{
	ti->ops->declare(ti, out, name, name_len);
}

/* Define the type */
static void define_type(struct type_info *ti, FILE *c_out, FILE *h_out)
{
	ti->ops->define(ti, c_out, h_out);
}

/* Generate a finalizer call for the type */
static void call_fini(struct type_info *ti, FILE *out, const char *var_expr)
{
	ti->ops->call_fini(ti, out, var_expr);
}


static void codegen_init(struct codegen *codegen)
{
	codegen->type_infos_to_destroy = NULL;
	codegen->defs = NULL;
	codegen->n_defs = 0;
}

static void codegen_fini(struct codegen *codegen)
{
	struct type_info *ti, *next;

	for (ti = codegen->type_infos_to_destroy; ti;) {
		next = ti->next_to_destroy;
		ti->ops->destroy(ti);
		ti = next;
	}

	free(codegen->defs);
}

static struct type_info *resolve_type(struct codegen *codegen,
				      struct eu_variant *schema);

static void add_type_info_to_destroy(struct codegen *codegen,
				     struct type_info *ti)
{
	ti->next_to_destroy = codegen->type_infos_to_destroy;
	codegen->type_infos_to_destroy = ti;
}


/* Simple value types (numbers, etc.) */

struct simple_type_info {
	struct type_info base;
	const char *type_name;
};

static void simple_type_declare(struct type_info *ti, FILE *out,
				const char *name, size_t name_len)
{
	struct simple_type_info *sti = (void *)ti;

	fprintf(out, "\t%s %.*s;\n", sti->type_name, (int)name_len, name);
}

static void noop_fill(struct type_info *ti, struct codegen *codegen,
		      struct eu_variant *schema)
{
	(void)ti;
	(void)codegen;
	(void)schema;
}

static void noop_define(struct type_info *ti, FILE *c_out, FILE *h_out)
{
	(void)ti;
	(void)c_out;
	(void)h_out;
}

static void noop_call_fini(struct type_info *ti, FILE *out,
			   const char *var_expr)
{
	(void)ti;
	(void)out;
	(void)var_expr;
}

struct type_info_ops simple_type_info_ops = {
	noop_fill,
	simple_type_declare,
	noop_define,
	noop_call_fini,
	NULL
};

#define DEFINE_SIMPLE_TYPE_INFO(name, type_name, metadata_ptr_expr)   \
struct  simple_type_info name = {                                     \
	{                                                             \
		&simple_type_info_ops,                                \
		metadata_ptr_expr,                                    \
		NULL                                                  \
	},                                                            \
	type_name                                                     \
}

DEFINE_SIMPLE_TYPE_INFO(number_type_info, "double",
			 "&eu_number_metadata");
DEFINE_SIMPLE_TYPE_INFO(boolean_type_info, "eu_bool_t",
			 "&eu_bool_metadata");


/* Builtin types (strings, etc.) */

struct builtin_type_info {
	struct simple_type_info base;
	const char *fini_func;
};

static void builtin_type_call_fini(struct type_info *ti, FILE *out,
				   const char *var_expr)
{
	struct builtin_type_info *bti = (void *)ti;

	fprintf(out, "\t%s(&%s);\n", bti->fini_func, var_expr);
}

struct type_info_ops builtin_type_info_ops = {
	noop_fill,
	simple_type_declare,
	noop_define,
	builtin_type_call_fini,
	NULL
};

#define DEFINE_BUILTIN_TYPE_INFO(name, type_name, metadata_ptr_expr, fini_func) \
struct  builtin_type_info name = {                                    \
	{                                                             \
		{                                                     \
			&builtin_type_info_ops,                       \
			metadata_ptr_expr,                            \
			NULL                                          \
		},                                                    \
		type_name,                                            \
	},                                                            \
	fini_func                                                     \
}

DEFINE_BUILTIN_TYPE_INFO(string_type_info, "struct eu_string",
			 "&eu_string_metadata",
			 "eu_string_fini");

DEFINE_BUILTIN_TYPE_INFO(variant_type_info, "struct eu_variant",
			 "&eu_variant_metadata",
			 "eu_variant_fini");


/* Structs */

struct member_info {
	const char *name;
	size_t name_len;
	struct type_info *type;
};

struct struct_type_info {
	struct type_info base;

	const char *struct_name;
	size_t struct_name_len;

	struct member_info *members;
	size_t members_len;

	char *metadata_name;
	char *inline_metadata_name;

	int defined_yet;
};

static struct type_info_ops struct_type_info_ops;

static struct type_info *alloc_struct(struct eu_variant *schema,
				      struct codegen *codegen)
{
	struct struct_type_info *sti = xalloc(sizeof *sti);
	struct eu_variant *name = eu_variant_get_cstr(schema,
						      "euphemusStructName");

	assert(name && eu_variant_type(name) == EU_JSON_STRING);

	sti->base.ops = &struct_type_info_ops;
	sti->struct_name = name->u.string.chars;
	sti->struct_name_len = name->u.string.len;
	sti->members = NULL;
	sti->members_len = 0;
	sti->defined_yet = 0;

	sti->metadata_name
		= xsprintf("struct_%.*s_metadata",
			   (int)sti->struct_name_len, sti->struct_name);
	sti->inline_metadata_name
		= xsprintf("inline_struct_%.*s_metadata",
			   (int)sti->struct_name_len, sti->struct_name);
	sti->base.metadata_ptr_expr
		= xsprintf("&%s.base", sti->metadata_name);

	add_type_info_to_destroy(codegen, &sti->base);
	return &sti->base;
}

static void struct_fill(struct type_info *ti, struct codegen *codegen,
			struct eu_variant *schema)
{
	struct struct_type_info *sti = (void *)ti;
	struct eu_variant *props = eu_variant_get_cstr(schema,
						       "properties");
	struct eu_variant_members *props_members;
	size_t i;

	assert(props && eu_variant_type(props) == EU_JSON_OBJECT);

	props_members = &props->u.object.members;
	assert(!sti->members);
	sti->members = xalloc(props_members->len * sizeof *sti->members);
	sti->members_len = props_members->len;

	for (i = 0; i < props_members->len; i++) {
		struct eu_variant_member *sm = &props_members->members[i];
		struct member_info *mi = &sti->members[i];

		mi->name = sm->name;
		mi->name_len = sm->name_len;
		mi->type = resolve_type(codegen, &sm->value);
	}
}


static void struct_declare(struct type_info *ti, FILE *out,
			   const char *name, size_t name_len)
{
	struct struct_type_info *sti = (void *)ti;

	fprintf(out, "\tstruct %.*s *%.*s;\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)name_len, name);
}

static void struct_define(struct type_info *ti, FILE *c_out, FILE *h_out)
{
	struct struct_type_info *sti = (void *)ti;
	size_t i;

	if (sti->defined_yet)
		return;

	sti->defined_yet = 1;

	/* The definitions of any types used in this struct. */
	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		define_type(mi->type, c_out, h_out);
	}

	/* The definition of the struct itself. */
	fprintf(h_out, "struct %.*s {\n",
		(int)sti->struct_name_len, sti->struct_name);

	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		declare(mi->type, h_out, mi->name, mi->name_len);
	}

	fprintf(h_out,
		"\tstruct eu_variant_members extras;\n"
		"};\n\n");

	/* Member metadata */
	fprintf(c_out,
		"static struct eu_struct_member %.*s_members[%d] = {\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->members_len);

	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];

		/* TODO need to escape member name below */
		fprintf(c_out,
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
			mi->type->metadata_ptr_expr);
	}

	fprintf(c_out, "};\n\n");

	/* Definitiion of the eu_struct_metadata instance */
	fprintf(h_out, "extern struct eu_struct_metadata %s;\n",
		sti->metadata_name);

	fprintf(c_out,
		"struct eu_struct_metadata %s\n"
		"\t= EU_STRUCT_METADATA_INITIALIZER(struct %.*s, "
			"%.*s_members);\n\n",
		sti->metadata_name,
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	/* Definitiion of the eu_struct_metadata instance for inline
	   structs.  Inline support is incomplete currently. */
	fprintf(h_out, "extern struct eu_struct_metadata %s;\n\n",
		sti->inline_metadata_name);

	fprintf(c_out,
		"struct eu_struct_metadata %s\n"
		"\t= EU_INLINE_STRUCT_METADATA_INITIALIZER(struct %.*s, "
			"%.*s_members);\n\n",
		sti->inline_metadata_name,
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	fprintf(h_out,
		"void %.*s_fini(struct %.*s *p);\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	fprintf(c_out,
		"void %.*s_fini(struct %.*s *p)\n"
		"{\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		char *var;

		var = xsprintf("p->%.*s", (int)mi->name_len, mi->name);
		call_fini(mi->type, c_out, var);
		free(var);
	}

	fprintf(c_out, "\teu_variant_members_fini(&p->extras);\n");
	fprintf(c_out, "}\n\n");

	fprintf(h_out,
		"void %.*s_destroy(struct %.*s *p);\n\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	fprintf(c_out,
		"void %.*s_destroy(struct %.*s *p)\n"
		"{\n"
		"\t%.*s_fini(p);\n"
		"\tfree(p);\n"
		"}\n\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	fprintf(h_out,
		"void eu_parse_init_struct_%.*s(struct eu_parse *ep, struct %.*s **p);\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	fprintf(c_out,
		"void eu_parse_init_struct_%.*s(struct eu_parse *ep, struct %.*s **p)\n"
		"{\n"
		"\teu_parse_init(ep, %s, p);\n"
		"}\n\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name,
		sti->base.metadata_ptr_expr);

	fprintf(h_out,
		"void eu_parse_init_inline_struct_%.*s(struct eu_parse *ep, struct %.*s *p);\n\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name);

	fprintf(c_out,
		"void eu_parse_init_inline_struct_%.*s(struct eu_parse *ep, struct %.*s *p)\n"
		"{\n"
		"\teu_parse_init(ep, &%s.base, p);\n"
		"}\n\n",
		(int)sti->struct_name_len, sti->struct_name,
		(int)sti->struct_name_len, sti->struct_name,
		sti->inline_metadata_name);
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
	struct_fill,
	struct_declare,
	struct_define,
	struct_generate_fini,
	struct_destroy
};

#define REF_PREFIX "#/definitions/"
#define REF_PREFIX_LEN 14

static struct definition *find_def(struct codegen *codegen,
				   struct eu_variant *ref)
{
	size_t i;
	struct eu_string_value name;

	assert(eu_variant_type(ref) == EU_JSON_STRING);

	name = eu_string_value(ref->u.string.chars, ref->u.string.len);
	if (name.len < REF_PREFIX_LEN
	    || memcmp(name.chars, REF_PREFIX, REF_PREFIX_LEN))
		die("only refs beginning with '" REF_PREFIX "' are supported");

	name.chars += REF_PREFIX_LEN;
	name.len -= REF_PREFIX_LEN;

	for (i = 0; i < codegen->n_defs; i++) {
		struct definition *def = &codegen->defs[i];

		if (def->name.len == name.len
		    && !memcmp(def->name.chars, name.chars, name.len))
			return def;
	}

	die("unknown definition '%.*s'", (int)name.len, name.chars);
}

static struct type_info *resolve_ref(struct codegen *codegen,
				     struct eu_variant *ref)
{
	struct definition *def = find_def(codegen, ref);

	assert(def->state == DEF_TYPE || def->state == DEF_SCHEMA_TYPE);
	return def->u.type;
}

static struct type_info *alloc_type(struct codegen *codegen,
				    struct eu_variant *schema)
{
	struct eu_variant *type;

	type = eu_variant_get_cstr(schema, "type");
	assert(type && eu_variant_type(type) == EU_JSON_STRING);

	if (eu_variant_equals_cstr(type, "string"))
		return &string_type_info.base.base;
	else if (eu_variant_equals_cstr(type, "number"))
		return &number_type_info.base;
	else if (eu_variant_equals_cstr(type, "boolean"))
		return &boolean_type_info.base;
	else if (eu_variant_equals_cstr(type, "object"))
		return alloc_struct(schema, codegen);
	else
		die("unknown type \"%.*s\"", (int)type->u.string.len,
		    type->u.string.chars);
}

static struct type_info *resolve_type(struct codegen *codegen,
				      struct eu_variant *schema)
{
	struct eu_variant *ref;
	struct type_info *ti;

	assert(eu_variant_type(schema) == EU_JSON_OBJECT);

	if (!schema->u.object.members.len)
		/* An empty schema */
		return &variant_type_info.base.base;

	ref = eu_variant_get_cstr(schema, "$ref");
	if (ref) {
		assert(schema->u.object.members.len == 1);
		return resolve_ref(codegen, ref);
	}

	ti = alloc_type(codegen, schema);
	fill_type(ti, codegen, schema);
	return ti;
}

static void remove_extension(char *c)
{
	char *last_dot = NULL;

	for (; *c; c++) {
		if (*c == '.')
			last_dot = c;
		else if (*c == '/')
			last_dot = NULL;
	}

	if (last_dot)
		*last_dot = 0;
}

static void codegen_prolog(const char *out_path, FILE *c_out, FILE *h_out)
{
	fprintf(h_out, "#include \"euphemus.h\"\n\n");

	fprintf(c_out,
		"#include <stddef.h>\n\n"
		"#include \"%s\"\n"
		"#include \"euphemus.h\"\n\n",
		out_path);
}

static struct type_info *alloc_definition(struct codegen *codegen,
					  struct definition *def)
{
	switch (def->state) {
	case DEF_SCHEMA:
		def->u.type = alloc_type(codegen, def->u.schema);
		def->state = DEF_SCHEMA_TYPE;
		break;

	case DEF_REF:
		def->state = DEF_RESOLVING;
		def->u.type = alloc_definition(codegen, def->u.ref);
		def->state = DEF_TYPE;
		break;

	case DEF_RESOLVING:
		die("circular reference");

	case DEF_SCHEMA_TYPE:
	case DEF_TYPE:
		break;

	default:
		abort();
	}

	return def->u.type;
}

static void codegen_definitions(struct codegen *codegen,
				struct eu_variant *definitions,
				FILE *c_out, FILE *h_out)
{
	struct eu_variant_members *sdefs;
	size_t i;

	if (!definitions)
		return;

	assert(eu_variant_type(definitions) == EU_JSON_OBJECT);

	sdefs = &definitions->u.object.members;

	codegen->n_defs = sdefs->len;
	codegen->defs = xalloc(sdefs->len * sizeof *codegen->defs);

	/* Fill in name field of definitions so that find_def works. */
	for (i = 0; i < sdefs->len; i++) {
		struct eu_variant_member *sdef = &sdefs->members[i];
		struct definition *cdef = &codegen->defs[i];

		cdef->name = eu_string_value(sdef->name, sdef->name_len);
	}

	/* Identify definitions which directly contain a reference,
	   and the target definitions in such cases. */
	for (i = 0; i < sdefs->len; i++) {
		struct definition *def = &codegen->defs[i];
		struct eu_variant *schema = &sdefs->members[i].value;
		struct eu_variant *ref;

		assert(eu_variant_type(schema) == EU_JSON_OBJECT);

		ref = eu_variant_get_cstr(schema, "$ref");
		if (!ref) {
			def->state = DEF_SCHEMA;
			def->u.schema = schema;
		}
		else {
			assert(schema->u.object.members.len == 1);
			def->state = DEF_REF;
			def->u.ref = find_def(codegen, ref);
		}
	}

	/* Allocate all definition schemas and resolve definitions
	   which directly contain a reference. */
	for (i = 0; i < sdefs->len; i++)
		alloc_definition(codegen, &codegen->defs[i]);

	/* Fill any non-ref definition schemas */
	for (i = 0; i < sdefs->len; i++) {
		struct definition *def = &codegen->defs[i];
		struct eu_variant *schema = &sdefs->members[i].value;

		if (def->state == DEF_SCHEMA_TYPE)
			fill_type(def->u.type, codegen, schema);
	}

	/* And finally, actually codegen the non-ref schemas */
	for (i = 0; i < sdefs->len; i++) {
		struct definition *def = &codegen->defs[i];
		if (def->state == DEF_SCHEMA_TYPE)
			define_type(def->u.type, c_out, h_out);
	}
}

static void codegen(const char *path, struct eu_variant *schema)
{
	struct codegen codegen;
	FILE *c_out, *h_out;
	char *out_path;
	char *basename = xstrdup(path);

	remove_extension(basename);
	out_path = xsprintf("%s.c", basename);
	c_out = fopen(out_path, "w");
	if (!c_out)
		die("error opening \"%s\": %s", out_path, strerror(errno));

	free(out_path);

	out_path = xsprintf("%s.h", basename);
	h_out = fopen(out_path, "w");
	if (!h_out)
		die("error opening \"%s\": %s", out_path, strerror(errno));

	codegen_prolog(out_path, c_out, h_out);

	codegen_init(&codegen);

	/* Generate code for definitions */
	assert(eu_variant_type(schema) == EU_JSON_OBJECT);
	codegen_definitions(&codegen,
			    eu_variant_get_cstr(schema, "definitions"),
			    c_out, h_out);

	/* Generate code for the main schema */
	define_type(resolve_type(&codegen, schema), c_out, h_out);
	codegen_fini(&codegen);

	fclose(c_out);
	fclose(h_out);
	free(out_path);
	free(basename);
}

int main(int argc, char **argv)
{
	int i;
	struct eu_variant var;

	for (i = 1; i < argc; i++) {
		parse_schema_file(argv[i], &var);
		codegen(argv[1], &var);
		eu_variant_fini(&var);
	}

	return 0;
}
