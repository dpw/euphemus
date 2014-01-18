#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>

#include <euphemus.h>

static void error(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

static void error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

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

/* The codegen object holds everything for code generation */

struct definition {
	struct eu_string_ref name;
	enum {
		DEF_SCHEMA,
		DEF_REF,
		DEF_RESOLVING,
		DEF_SCHEMA_TYPE,
		DEF_TYPE
	} state;
	union {
		struct eu_value schema;
		struct definition *ref;
		struct type_info *type;
	} u;
};

struct codegen {
	int inline_funcs;

	const char *source_path;
	char *c_out_path;
	char *h_out_path;

	FILE *c_out;
	FILE *h_out;
	struct type_info *type_infos_to_destroy;

	struct definition *defs;
	size_t n_defs;

	struct type_info *number_type;
	struct type_info *bool_type;
	struct type_info *string_type;
	struct type_info *variant_type;
};

struct type_info {
	struct type_info_ops *ops;
	struct type_info *next_to_destroy;

	/* The basis for constructed names related to the type */
	const char *base_name;

	/* The C expression for a pointer to the type's metadata */
	const char *metadata_ptr_expr;

	/* The name of the C *_members and *member structs, or NULL if
	the definitions were not emitted yet */
	const char *members_struct_name;
	const char *member_struct_name;

	eu_bool_t is_pointer;
};

struct type_info_ops {
	void (*fill)(struct type_info *ti, struct codegen *codegen,
		     struct eu_value schema);
	void (*declare)(struct type_info *ti, FILE *out, const char *name);
	void (*define)(struct type_info *ti, struct codegen *codegen);
	void (*call_fini)(struct type_info *ti, FILE *out,
			  const char *var_expr);
	void (*destroy)(struct type_info *ti);
};

/* Fill in sub-schema information. */
static void fill_type(struct type_info *ti, struct codegen *codegen,
		      struct eu_value schema)
{
	ti->ops->fill(ti, codegen, schema);
}

/* Declare a variable or field of the given type. */
static void declare(struct type_info *ti, FILE *out, const char *name)
{
	ti->ops->declare(ti, out, name);
}

/* Define the type */
static void define_type(struct type_info *ti, struct codegen *codegen)
{
	ti->ops->define(ti, codegen);
}

/* Generate a finalizer call for the type */
static void call_fini(struct type_info *ti, FILE *out, const char *var_expr)
{
	ti->ops->call_fini(ti, out, var_expr);
}

static void upcase_print(FILE *out, const char *s)
{
	while (*s)
		putc(toupper(*s++), out);
}

/* Emitting the definition of the *_members struct for the type, if
   not done already. */
static void define_members_struct(struct type_info *ti, struct codegen *codegen)
{
	if (ti->members_struct_name)
		return;

	define_type(ti, codegen);

	ti->members_struct_name = xsprintf("%s_members", ti->base_name);
	ti->member_struct_name = xsprintf("%s_member", ti->base_name);

	fprintf(codegen->h_out, "#ifndef ");
	upcase_print(codegen->h_out, ti->members_struct_name);
	fprintf(codegen->h_out, "_DEFINED\n");
	fprintf(codegen->h_out, "#define ");
	upcase_print(codegen->h_out, ti->members_struct_name);
	fprintf(codegen->h_out, "_DEFINED\n");

	/* The *_member struct declaration */
	fprintf(codegen->h_out,
		"struct %s {\n"
		"\tstruct eu_string_ref name;\n",
		ti->member_struct_name);
	declare(ti, codegen->h_out, "value");
	fprintf(codegen->h_out, "};\n\n");

	/* The *_members struct declaration */
	fprintf(codegen->h_out,
		"struct %s {\n"
		"\tstruct %s *members;\n"
		"\tsize_t len;\n"
		"\tstruct {\n"
		"\t\tsize_t capacity;\n"
		"\t} priv;\n"
		"};\n",
		ti->members_struct_name, ti->member_struct_name);

	fprintf(codegen->h_out, "#endif\n\n");
}

static struct type_info *resolve_type(struct codegen *codegen,
				      struct eu_value schema);

static void type_info_init(struct type_info *ti,
			   struct codegen *codegen,
			   struct type_info_ops *ops,
			   const char *base_name,
			   const char *metadata_ptr_expr,
			   eu_bool_t is_pointer)
{
	ti->ops = ops;
	ti->base_name = base_name;
	ti->metadata_ptr_expr = metadata_ptr_expr;
	ti->members_struct_name = NULL;
	ti->member_struct_name = NULL;
	ti->is_pointer = is_pointer;

	ti->next_to_destroy = codegen->type_infos_to_destroy;
	codegen->type_infos_to_destroy = ti;
}

static void type_info_fini(struct type_info *ti)
{
	free((void *)ti->members_struct_name);
	free((void *)ti->member_struct_name);
}

/* Simple value types (numbers, etc.) */

struct simple_type_info {
	struct type_info base;
	const char *type_name;
};

static void simple_type_declare(struct type_info *ti, FILE *out,
				const char *name)
{
	struct simple_type_info *sti = (void *)ti;

	fprintf(out, "\t%s %s;\n", sti->type_name, name);
}

static void noop_fill(struct type_info *ti, struct codegen *codegen,
		      struct eu_value schema)
{
	(void)ti;
	(void)codegen;
	(void)schema;
}

static void noop_define(struct type_info *ti, struct codegen *codegen)
{
	(void)ti;
	(void)codegen;
}

static void noop_call_fini(struct type_info *ti, FILE *out,
			   const char *var_expr)
{
	(void)ti;
	(void)out;
	(void)var_expr;
}

struct type_info *make_simple_type(struct codegen *codegen,
				   struct type_info_ops *ops,
				   const char *c_type_name,
				   const char *base_name)
{
	struct simple_type_info *sti = xalloc(sizeof *sti);

	type_info_init(&sti->base, codegen, ops, base_name,
		       xsprintf("&%s_metadata", base_name), 0);

	sti->type_name = c_type_name;
	return &sti->base;
}

static void simple_type_destroy(struct type_info *ti)
{
	struct simple_type_info *sti = (void *)ti;

	type_info_fini(ti);
	free((void *)sti->base.metadata_ptr_expr);
	free(sti);
}

struct type_info_ops simple_type_info_ops = {
	noop_fill,
	simple_type_declare,
	noop_define,
	noop_call_fini,
	simple_type_destroy
};

static void builtin_type_call_fini(struct type_info *ti, FILE *out,
				   const char *var_expr)
{
	fprintf(out, "\t%s_fini(&%s);\n", ti->base_name, var_expr);
}

struct type_info_ops builtin_type_info_ops = {
	noop_fill,
	simple_type_declare,
	noop_define,
	builtin_type_call_fini,
	simple_type_destroy
};

static char *remove_extension(const char *path)
{
	char *res = xstrdup(path);
	char *c;
	char *last_dot = NULL;

	for (c = res; *c; c++) {
		if (*c == '.')
			last_dot = c;
		else if (*c == '/')
			last_dot = NULL;
	}

	if (last_dot)
		*last_dot = 0;

	return res;
}

static void codegen_init(struct codegen *codegen, const char *source_path)
{
	char *basename;

	codegen->inline_funcs = 1;

	codegen->source_path = source_path;
	basename = remove_extension(source_path);
	codegen->c_out_path = xsprintf("%s.c", basename);
	codegen->h_out_path = xsprintf("%s.h", basename);
	free(basename);

	codegen->c_out = codegen->h_out = NULL;
	codegen->type_infos_to_destroy = NULL;
	codegen->defs = NULL;
	codegen->n_defs = 0;

	codegen->number_type = make_simple_type(codegen, &simple_type_info_ops,
						"double", "eu_number");
	codegen->bool_type = make_simple_type(codegen, &simple_type_info_ops,
					      "eu_bool_t", "eu_bool");
	codegen->string_type = make_simple_type(codegen, &builtin_type_info_ops,
						"struct eu_string",
						"eu_string");
	codegen->variant_type = make_simple_type(codegen,
						 &builtin_type_info_ops,
						 "struct eu_variant",
						 "eu_variant");
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

	free(codegen->c_out_path);
	free(codegen->h_out_path);

	if (codegen->c_out)
		fclose(codegen->c_out);
	if (codegen->h_out)
		fclose(codegen->h_out);
}

static int codegen_open_output_files(struct codegen *codegen)
{
	int ok = 0;

	codegen->c_out = fopen(codegen->c_out_path, "w");
	if (!codegen->c_out) {
		error("%s: error opening \"%s\": %s", codegen->source_path,
		      codegen->c_out_path, strerror(errno));
		goto out;
	}

	codegen->h_out = fopen(codegen->h_out_path, "w");
	if (!codegen->h_out) {
		error("%s: error opening \"%s\": %s", codegen->source_path,
		      codegen->h_out_path, strerror(errno));
		goto out;
	}

	ok = 1;
 out:
	return ok;
}

static void codegen_delete_output_files(struct codegen *codegen)
{
	if (codegen->c_out) {
		fclose(codegen->c_out);
		codegen->c_out = NULL;
		remove(codegen->c_out_path);
	}

	if (codegen->h_out) {
		fclose(codegen->h_out);
		codegen->h_out = NULL;
		remove(codegen->h_out_path);
	}
}

/* Structs */

struct member_info {
	struct eu_string_ref json_name;
	char *c_name;
	struct type_info *type;
};

struct struct_type_info {
	struct type_info base;

	struct eu_string_ref struct_name;

	struct member_info *members;
	size_t members_len;

	struct type_info *extras_type;

	char *ptr_metadata_name;
	char *inline_metadata_name;

	char defined_yet;
};

static struct type_info_ops struct_type_info_ops;

static struct type_info *alloc_struct(struct eu_value schema,
				      struct codegen *codegen)
{
	struct struct_type_info *sti = xalloc(sizeof *sti);
	struct eu_value name = eu_value_get_cstr(schema, "euphemusStructName");

	assert(eu_value_ok(name) && eu_value_type(name) == EU_JSON_STRING);

	sti->struct_name = eu_value_to_string_ref(name);
	sti->ptr_metadata_name
		= xsprintf("struct_%.*s_ptr_metadata",
			   (int)sti->struct_name.len, sti->struct_name.chars);
	sti->inline_metadata_name
		= xsprintf("struct_%.*s_metadata",
			   (int)sti->struct_name.len, sti->struct_name.chars);
	sti->extras_type = NULL;
	sti->members = NULL;
	sti->members_len = 0;
	sti->defined_yet = 0;

	type_info_init(&sti->base, codegen, &struct_type_info_ops,
		       xsprintf("struct_%.*s",
			    (int)sti->struct_name.len, sti->struct_name.chars),
		       xsprintf("&%s.base", sti->ptr_metadata_name), 1);

	return &sti->base;
}

/* Convert a JSON string to one that is a valid C identifier. */
static char *sanitize_name(struct eu_string_ref name)
{
	char *res = malloc(name.len + 1);
	size_t i;
	char *p;

	for (i = 0, p = res; i < name.len; i++) {
		if (isalnum(name.chars[i]))
			*p++ = name.chars[i];
	}

	*p = 0;
	return res;
}

static void struct_fill(struct type_info *ti, struct codegen *codegen,
			struct eu_value schema)
{
	struct struct_type_info *sti = (void *)ti;
	struct eu_value props, additional_props;

	props = eu_value_get_cstr(schema, "properties");
	if (eu_value_ok(props)) {
		struct eu_object_iter pi;
		size_t i;

		assert(eu_value_type(props) == EU_JSON_OBJECT);

		assert(!sti->members);
		sti->members_len = eu_object_size(props);
		sti->members
			= xalloc(sti->members_len * sizeof *sti->members);

		for (eu_object_iter_init(&pi, props), i = 0;
		     eu_object_iter_next(&pi);
		     i++) {
			struct member_info *mi = &sti->members[i];

			mi->json_name = pi.name;
			mi->c_name = sanitize_name(mi->json_name);
			mi->type = resolve_type(codegen, pi.value);
		}
	}

	additional_props
		= eu_value_get_cstr(schema, "additionalProperties");
	if (eu_value_ok(additional_props)) {
		assert(eu_value_type(additional_props) == EU_JSON_OBJECT);
		sti->extras_type = resolve_type(codegen, additional_props);
	}
}


static void struct_declare(struct type_info *ti, FILE *out, const char *name)
{
	struct struct_type_info *sti = (void *)ti;

	fprintf(out, "\tstruct %.*s *%s;\n",
		(int)sti->struct_name.len, sti->struct_name.chars, name);
}

static void emit_inlinish_func_decl(struct codegen *codegen, const char *fmt,
				    ...)
	__attribute__ ((format (printf, 2, 3)));

static void emit_inlinish_func_decl(struct codegen *codegen, const char *fmt,
				    ...)
{
	va_list ap;
	FILE *out;

	if (codegen->inline_funcs) {
		out = codegen->h_out;
		fprintf(out, "static __inline__ ");
	}
	else {
		va_start(ap, fmt);
		vfprintf(codegen->h_out, fmt, ap);
		va_end(ap);
		fprintf(codegen->h_out, ";\n");

		out = codegen->c_out;
	}

	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fprintf(out, "\n");
}

static void emit_inlinish_func_body(struct codegen *codegen, const char *fmt,
				    ...)
	__attribute__ ((format (printf, 2, 3)));

static void emit_inlinish_func_body(struct codegen *codegen, const char *fmt,
				    ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(codegen->inline_funcs ? codegen->h_out : codegen->c_out,
		 fmt, ap);
	va_end(ap);
}

static void struct_define_converters(struct struct_type_info *sti,
				     struct codegen *codegen)
{
	emit_inlinish_func_decl(codegen,
			"struct eu_value %.*s_ptr_to_eu_value(struct %.*s **p)",
			(int)sti->struct_name.len, sti->struct_name.chars,
			(int)sti->struct_name.len, sti->struct_name.chars);
	emit_inlinish_func_body(codegen,
				"{\n"
				"\treturn eu_value(p, %s);\n"
				"}\n\n",
				sti->base.metadata_ptr_expr);

	emit_inlinish_func_decl(codegen,
			"struct eu_value %.*s_to_eu_value(struct %.*s *p)",
			(int)sti->struct_name.len, sti->struct_name.chars,
			(int)sti->struct_name.len, sti->struct_name.chars);
	emit_inlinish_func_body(codegen,
				"{\n"
				"\treturn eu_value(p, &%s.base);\n"
				"}\n\n",
				sti->inline_metadata_name);
}

static void struct_define(struct type_info *ti, struct codegen *codegen)
{
	struct struct_type_info *sti = (void *)ti;
	size_t i;
	int presence_count;
	struct type_info *extras_type;

	if (sti->defined_yet)
		return;

	sti->defined_yet = 1;

	/* The definitions of any types used in this struct. */
	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		define_type(mi->type, codegen);
	}

	/* The definition of the extras_type */
	extras_type = sti->extras_type;
	if (!extras_type)
		extras_type = codegen->variant_type;

	define_members_struct(extras_type, codegen);

	/* Count how many presence bits we need */
	for (i = 0, presence_count = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		if (!mi->type->is_pointer)
			presence_count++;
	}

	/* The definition of the struct itself. */
	fprintf(codegen->h_out, "struct %.*s {\n",
		(int)sti->struct_name.len, sti->struct_name.chars);

	if (presence_count)
		fprintf(codegen->h_out,
		    "\tunsigned char presence_bits[(%d - 1) / CHAR_BIT + 1];\n",
			presence_count);

	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		declare(mi->type, codegen->h_out, mi->c_name);
	}

	fprintf(codegen->h_out,
		"\tstruct %s extras;\n"
		"};\n\n",
		extras_type->members_struct_name);

	/* Member metadata */
	fprintf(codegen->c_out,
		"static struct eu_struct_member %.*s_members[%d] = {\n",
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->members_len);

	for (i = 0, presence_count = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];

		fprintf(codegen->c_out,
			"\t{\n"
			"\t\toffsetof(struct %.*s, %s),\n" /* offset */
			"\t\t%d,\n", /* name_len */
			(int)sti->struct_name.len, sti->struct_name.chars,
			mi->c_name,
			(int)mi->json_name.len);

		if (mi->type->is_pointer) {
			fprintf(codegen->c_out,
				"\t\t-1, 0,\n");
		}
		else {
			fprintf(codegen->c_out,
				"\t\t%d / CHAR_BIT, 1 << (%d %% CHAR_BIT),\n",
				presence_count,
				presence_count);
			presence_count++;
		}

		/* TODO need to escape member name */
		fprintf(codegen->c_out,
			"\t\t\"%.*s\",\n" /* name */
			"\t\t%s\n" /* metadata */
			"\t},\n",
			(int)mi->json_name.len, mi->json_name.chars,
			mi->type->metadata_ptr_expr);
	}

	fprintf(codegen->c_out, "};\n\n");

	/* Definitiion of the eu_struct_metadata instance */
	fprintf(codegen->h_out, "extern struct eu_struct_metadata %s;\n",
		sti->ptr_metadata_name);

	fprintf(codegen->c_out,
		"struct eu_struct_metadata %s\n"
		"\t= EU_STRUCT_PTR_METADATA_INITIALIZER(struct %.*s, "
			"%.*s_members, struct %s, %s);\n\n",
		sti->ptr_metadata_name,
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars,
		extras_type->member_struct_name,
		extras_type->metadata_ptr_expr);

	/* Definition of the eu_struct_metadata instance for inline
	   structs.  Inline support is incomplete currently. */
	fprintf(codegen->h_out, "extern struct eu_struct_metadata %s;\n\n",
		sti->inline_metadata_name);

	fprintf(codegen->c_out,
		"struct eu_struct_metadata %s\n"
		"\t= EU_STRUCT_METADATA_INITIALIZER(struct %.*s, "
			"%.*s_members, struct %s, %s);\n\n",
		sti->inline_metadata_name,
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars,
		extras_type->member_struct_name,
		extras_type->metadata_ptr_expr);

	fprintf(codegen->h_out,
		"void %.*s_fini(struct %.*s *p);\n",
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars);

	fprintf(codegen->c_out,
		"void %.*s_fini(struct %.*s *p)\n"
		"{\n",
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars);

	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		char *var;

		var = xsprintf("p->%s", mi->c_name);
		call_fini(mi->type, codegen->c_out, var);
		free(var);
	}

	fprintf(codegen->c_out,
		"\tif (p->extras.len)\n"
		"\t\teu_struct_extras_fini(&%s, &p->extras);\n"
		"}\n\n",
		sti->ptr_metadata_name);

	fprintf(codegen->h_out,
		"void %.*s_destroy(struct %.*s *p);\n\n",
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars);

	fprintf(codegen->c_out,
		"void %.*s_destroy(struct %.*s *p)\n"
		"{\n"
		"\t%.*s_fini(p);\n"
		"\tfree(p);\n"
		"}\n\n",
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars);

	struct_define_converters(sti, codegen);
}

static void struct_destroy(struct type_info *ti)
{
	struct struct_type_info *sti = (void *)ti;
	size_t i;

	for (i = 0; i < sti->members_len; i++) {
		struct member_info *mi = &sti->members[i];
		free(mi->c_name);
	}

	type_info_fini(ti);
	free(sti->ptr_metadata_name);
	free(sti->inline_metadata_name);
	free((void *)sti->base.base_name);
	free((void *)sti->base.metadata_ptr_expr);
	free(sti->members);
	free(sti);
}

static void struct_generate_fini(struct type_info *ti, FILE *out,
				 const char *var_expr)
{
	struct struct_type_info *sti = (void *)ti;

	fprintf(out, "\tif (%s) %.*s_destroy(%s);\n",
		var_expr,
		(int)sti->struct_name.len, sti->struct_name.chars,
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
				   struct eu_value ref)
{
	size_t i;
	struct eu_string_ref name;

	assert(eu_value_type(ref) == EU_JSON_STRING);

	name = eu_value_to_string_ref(ref);
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
				     struct eu_value ref)
{
	struct definition *def = find_def(codegen, ref);
	assert(def->state == DEF_TYPE || def->state == DEF_SCHEMA_TYPE);
	return def->u.type;
}

static int is_empty_schema(struct eu_value schema)
{
	struct eu_object_iter i;

	for (eu_object_iter_init(&i, schema); eu_object_iter_next(&i);) {
		if (!eu_string_ref_equal(i.name, eu_cstr("definitions"))
		    && !eu_string_ref_equal(i.name, eu_cstr("title")))
			return 0;
	}

	return 1;
}

static struct type_info *alloc_type(struct codegen *codegen,
				    struct eu_value schema)
{
	struct eu_value type;
	struct eu_string_ref type_str;

	type = eu_value_get_cstr(schema, "type");
	if (!eu_value_ok(type)) {
		assert(is_empty_schema(schema));
		return codegen->variant_type;
	}

	assert(eu_value_type(type) == EU_JSON_STRING);
	type_str = eu_value_to_string_ref(type);

	if (eu_string_ref_equal(type_str, eu_cstr("string")))
		return codegen->string_type;
	else if (eu_string_ref_equal(type_str, eu_cstr("number")))
		return codegen->number_type;
	else if (eu_string_ref_equal(type_str, eu_cstr("boolean")))
		return codegen->bool_type;
	else if (eu_string_ref_equal(type_str, eu_cstr("object")))
		return alloc_struct(schema, codegen);
	else
		die("unknown type \"%.*s\"", (int)type_str.len, type_str.chars);
}

static struct type_info *resolve_type(struct codegen *codegen,
				      struct eu_value schema)
{
	struct eu_value ref;
	struct type_info *ti;

	assert(eu_value_type(schema) == EU_JSON_OBJECT);

	ref = eu_value_get_cstr(schema, "$ref");
	if (eu_value_ok(ref)) {
		assert(eu_object_size(schema) == 1);
		return resolve_ref(codegen, ref);
	}

	ti = alloc_type(codegen, schema);
	fill_type(ti, codegen, schema);
	return ti;
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
				struct eu_value definitions)
{
	struct eu_object_iter di;
	size_t i;

	if (!eu_value_ok(definitions))
		return;

	assert(eu_value_type(definitions) == EU_JSON_OBJECT);

	codegen->n_defs = eu_object_size(definitions);
	codegen->defs = xalloc(codegen->n_defs * sizeof *codegen->defs);

	/* Fill in name field of definitions so that find_def works. */
	for (eu_object_iter_init(&di, definitions), i = 0;
	     eu_object_iter_next(&di);
	     i++) {
		struct definition *cdef = &codegen->defs[i];
		cdef->name = di.name;
	}

	/* Identify definitions which directly contain a reference,
	   and the target definitions in such cases. */
	for (eu_object_iter_init(&di, definitions), i = 0;
	     eu_object_iter_next(&di);
	     i++) {
		struct definition *def = &codegen->defs[i];
		struct eu_value ref;

		assert(eu_value_type(di.value) == EU_JSON_OBJECT);

		ref = eu_value_get_cstr(di.value, "$ref");
		if (!eu_value_ok(ref)) {
			def->state = DEF_SCHEMA;
			def->u.schema = di.value;
		}
		else {
			assert(eu_object_size(di.value) == 1);
			def->state = DEF_REF;
			def->u.ref = find_def(codegen, ref);
		}
	}

	/* Allocate all definition schemas and resolve definitions
	   which directly contain a reference. */
	for (i = 0; i < codegen->n_defs; i++)
		alloc_definition(codegen, &codegen->defs[i]);

	/* Fill any non-ref definition schemas */
	for (eu_object_iter_init(&di, definitions), i = 0;
	     eu_object_iter_next(&di);
	     i++) {
		struct definition *def = &codegen->defs[i];

		if (def->state == DEF_SCHEMA_TYPE)
			fill_type(def->u.type, codegen, di.value);
	}

	/* And finally, actually codegen the non-ref schemas */
	for (i = 0; i < codegen->n_defs; i++) {
		struct definition *def = &codegen->defs[i];
		if (def->state == DEF_SCHEMA_TYPE)
			define_type(def->u.type, codegen);
	}
}

static void codegen_prolog(struct codegen *codegen)
{
	fprintf(codegen->h_out,
		"/* Generated from \"%s\".  You probably shouldn't edit this file. */\n\n"
		"#include <limits.h>\n"
		"#include <euphemus.h>\n\n",
		codegen->source_path);

	fprintf(codegen->c_out,
		"/* Generated from \"%s\".  You probably shouldn't edit this file. */\n\n"
		"#include <stddef.h>\n\n"
		"#include \"%s\"\n\n",
		codegen->source_path, codegen->h_out_path);
}

static int codegen(const char *path, struct eu_value schema)
{
	struct codegen codegen;
	int ok = 0;

	codegen_init(&codegen, path);

	if (!codegen_open_output_files(&codegen))
		goto out;

	codegen_prolog(&codegen);

	/* Generate code for definitions */
	assert(eu_value_type(schema) == EU_JSON_OBJECT);
	codegen_definitions(&codegen,
			    eu_value_get_cstr(schema, "definitions"));

	/* Generate code for the main schema */
	define_type(resolve_type(&codegen, schema), &codegen);

	ok = 1;
 out:
	if (!ok)
		codegen_delete_output_files(&codegen);

	codegen_fini(&codegen);
	return ok;
}

static int parse_schema_file(const char *path, struct eu_variant *var)
{
	struct eu_parse parse;
	FILE *fp = fopen(path, "r");
	int ok = 0;

	if (!fp) {
		error("%s: %s", path, strerror(errno));
		return 0;
	}

	eu_parse_init(&parse, eu_variant_value(var));

	while (!feof(fp)) {
		char buf[1000];
		size_t got = fread(buf, 1, 1000, fp);

		if (got < 1000 && ferror(fp)) {
			error("%s: %s", path, strerror(errno));
			goto out;
		}

		if (!eu_parse(&parse, buf, got)) {
			error("%s: parse error", path);
			goto out;
		}
	}

	if (!eu_parse_finish(&parse)) {
		error("%s: parse error", path);
		goto out;
	}

	ok = 1;

 out:
	fclose(fp);
	eu_parse_fini(&parse);
	return ok;
}

int main(int argc, char **argv)
{
	int i;
	struct eu_variant var;
	int all_ok = 1;

	for (i = 1; i < argc; i++) {
		if (!parse_schema_file(argv[i], &var)) {
			all_ok = 0;
			continue;
		}

		if (!codegen(argv[i], eu_variant_value(&var)))
			all_ok = 0;

		eu_variant_fini(&var);
	}

	return !all_ok;
}
