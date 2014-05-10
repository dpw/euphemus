#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>

#include <euphemus.h>

#include "schema_schema.h"

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
		/* The definition has a schema in u.schema */
		DEF_SCHEMA,

		/* The definition is a reference in u.ref */
		DEF_REF,

		/* We are in the process of resolving a DEF_REF */
		DEF_RESOLVING,

		/* The definition has a schema that has been processed
		   to a type_info in u.type */
		DEF_SCHEMA_TYPE,

		/* The definition is a reference that has been resolve
		   to u.type. */
		DEF_TYPE,

		/* The definition was found to be in error in some
		   way.  u.type gets set to NULL. */
		DEF_ERROR
	} state;
	union {
		struct schema *schema;
		struct definition *ref;
		struct type_info *type;
	} u;
};

struct codegen {
	int inline_funcs;
	int error_count;

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

enum optionality {
	REQUIRED,
	OPTIONAL
};

struct type_info {
	struct type_info_ops *ops;
	struct type_info *next_to_destroy;

	/* The basis for constructed C identifiers related to the type */
	const char *base_name;

	/* The C type name for use in declarations.  Includes a
	   trailing space if appropriate. */
	const char *c_type_name[2];

	/* The C expression for a pointer to the type's metadata */
	const char *metadata_ptr_expr[2];

	/* The C expression for a pointer to the type's descriptor */
	const char *descriptor_ptr_expr[2];

	/* The name of the C *_members and *_member structs containing
	values of this type, or NULL if the definitions were not
	emitted yet */
	const char *members_struct_name;
	const char *member_struct_name;

	eu_bool_t no_presence_bit;

	enum {
		NOT_DEFINED,
		DEFINING,
		DEFINED
	} defined;
};

struct type_info_ops {
	void (*fill)(struct type_info *ti, struct codegen *codegen,
		     struct schema *schema);
	void (*define)(struct type_info *ti, struct codegen *codegen);
	void (*call_fini)(struct type_info *ti, FILE *out,
			  const char *var_expr);
	void (*destroy)(struct type_info *ti);
};

/* Fill in sub-schema information. */
static void fill_type(struct type_info *ti, struct codegen *codegen,
		      struct schema *schema)
{
	ti->ops->fill(ti, codegen, schema);
}

/* Declare a variable or field of the given type. */
static void declare(struct type_info *ti, FILE *out, const char *name,
		    enum optionality optional)
{
	fprintf(out, "\t%s%s;\n", ti->c_type_name[optional], name);
}

static void upcase_print(FILE *out, const char *s)
{
	while (*s)
		putc(toupper(*s++), out);
}

static void define_member_struct(struct type_info *ti, struct codegen *codegen)
{
	fprintf(codegen->h_out, "#define ");
	upcase_print(codegen->h_out, ti->members_struct_name);
	fprintf(codegen->h_out, "_DEFINED\n\n");

	/* The *_member struct declaration */
	fprintf(codegen->h_out,
		"struct %s {\n"
		"\tstruct eu_string_ref name;\n",
		ti->member_struct_name);
	declare(ti, codegen->h_out, "value", REQUIRED);
	fprintf(codegen->h_out, "};\n\n");
}

/* Define the type */
static void define_type(struct type_info *ti, struct codegen *codegen)
{
	if (ti->defined != NOT_DEFINED)
		return;

	ti->defined = DEFINING;
	ti->ops->define(ti, codegen);
	ti->defined = DEFINED;

	if (ti->member_struct_name) {
		fprintf(codegen->h_out, "#ifndef ");
		upcase_print(codegen->h_out, ti->members_struct_name);
		fprintf(codegen->h_out, "_DEFINED\n");
		define_member_struct(ti, codegen);
		fprintf(codegen->h_out, "#endif\n\n");
	}
}

/* Generate a finalizer call for the type */
static void call_fini(struct type_info *ti, FILE *out, const char *var_expr)
{
	ti->ops->call_fini(ti, out, var_expr);
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

	if (ti->defined == DEFINED)
		define_member_struct(ti, codegen);

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
				      struct schema *schema,
				      struct eu_string_ref name_hint);

static void type_info_init(struct type_info *ti,
			   struct codegen *codegen,
			   struct type_info_ops *ops,
			   const char *base_name,
			   eu_bool_t no_presence_bit)
{
	ti->ops = ops;
	ti->base_name = base_name;
	ti->members_struct_name = NULL;
	ti->member_struct_name = NULL;
	ti->no_presence_bit = no_presence_bit;
	ti->defined = ops->define ? NOT_DEFINED : DEFINED;

	ti->next_to_destroy = codegen->type_infos_to_destroy;
	codegen->type_infos_to_destroy = ti;
}

static void free_expr_pair(const char **exprs)
{
	free((char *)exprs[REQUIRED]);
	if (exprs[OPTIONAL] != exprs[REQUIRED])
		free((char *)exprs[OPTIONAL]);
}

static void type_info_fini(struct type_info *ti)
{
	free_expr_pair(ti->c_type_name);
	free_expr_pair(ti->metadata_ptr_expr);
	free_expr_pair(ti->descriptor_ptr_expr);
	free((void *)ti->members_struct_name);
	free((void *)ti->member_struct_name);
}

static void noop_fill(struct type_info *ti, struct codegen *codegen,
		      struct schema *schema)
{
	(void)ti;
	(void)codegen;
	(void)schema;
}

static void noop_call_fini(struct type_info *ti, FILE *out,
			   const char *var_expr)
{
	(void)ti;
	(void)out;
	(void)var_expr;
}

/* Simple value types (numbers, etc.) */

static struct type_info *make_simple_or_builtin_type(struct codegen *codegen,
						     struct type_info_ops *ops,
						     const char *c_type_name,
						     const char *base_name,
						     eu_bool_t no_presence_bit)
{
	struct type_info *ti = xalloc(sizeof *ti);

	type_info_init(ti, codegen, ops, base_name, no_presence_bit);

	ti->c_type_name[REQUIRED]
		= ti->c_type_name[OPTIONAL]
		= xsprintf("%s ", c_type_name);

	ti->metadata_ptr_expr[REQUIRED]
		= ti->metadata_ptr_expr[OPTIONAL]
		= xsprintf("&%s_metadata", base_name);

	ti->descriptor_ptr_expr[REQUIRED]
		= ti->descriptor_ptr_expr[OPTIONAL]
		= xsprintf("&%s_descriptor", base_name);

	return ti;
}

static void simple_type_destroy(struct type_info *ti)
{
	type_info_fini(ti);
	free(ti);
}

struct type_info_ops simple_type_info_ops = {
	noop_fill,
	NULL,
	noop_call_fini,
	simple_type_destroy
};

struct type_info *make_simple_type(struct codegen *codegen,
				   const char *c_type_name,
				   const char *base_name)
{
	return make_simple_or_builtin_type(codegen, &simple_type_info_ops,
					   c_type_name, base_name, 0);
}

static void builtin_type_call_fini(struct type_info *ti, FILE *out,
				   const char *var_expr)
{
	fprintf(out, "\t%s_fini(&%s);\n", ti->base_name, var_expr);
}

struct type_info_ops builtin_type_info_ops = {
	noop_fill,
	NULL,
	builtin_type_call_fini,
	simple_type_destroy
};

struct type_info *make_builtin_type(struct codegen *codegen,
				   const char *c_type_name,
				   const char *base_name)
{
	return make_simple_or_builtin_type(codegen, &builtin_type_info_ops,
					   c_type_name, base_name, 1);
}

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
	char *base_path;

	codegen->inline_funcs = 1;
	codegen->error_count = 0;

	codegen->source_path = source_path;
	base_path = remove_extension(source_path);
	codegen->c_out_path = xsprintf("%s.c", base_path);
	codegen->h_out_path = xsprintf("%s.h", base_path);
	free(base_path);

	codegen->c_out = codegen->h_out = NULL;
	codegen->type_infos_to_destroy = NULL;
	codegen->defs = NULL;
	codegen->n_defs = 0;

	codegen->number_type = make_simple_type(codegen, "double", "eu_number");
	codegen->bool_type = make_simple_type(codegen, "eu_bool_t", "eu_bool");
	codegen->string_type = make_builtin_type(codegen, "struct eu_string",
						"eu_string");
	codegen->variant_type = make_builtin_type(codegen, "struct eu_variant",
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

static void codegen_error(struct codegen *codegen, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

static void codegen_error(struct codegen *codegen, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", codegen->source_path);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	codegen->error_count++;
}

static void codegen_open_output_files(struct codegen *codegen)
{
	codegen->c_out = fopen(codegen->c_out_path, "w");
	if (!codegen->c_out) {
		codegen_error(codegen, "error opening \"%s\": %s",
			      codegen->c_out_path, strerror(errno));
		return;
	}

	codegen->h_out = fopen(codegen->h_out_path, "w");
	if (!codegen->h_out) {
		codegen_error(codegen, "error opening \"%s\": %s",
			      codegen->h_out_path, strerror(errno));
		return;
	}
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

	char *metadata_func_name;
	char *ptr_metadata_func_name;
	char *descriptor_name;
};

static struct type_info_ops struct_type_info_ops;

static struct type_info *alloc_struct(struct schema *schema,
				      struct codegen *codegen,
				      struct eu_string_ref name)
{
	struct struct_type_info *sti = xalloc(sizeof *sti);

	if (schema->euphemusStructName.chars) {
		sti->struct_name = eu_string_to_ref(&schema->euphemusStructName);
	}
	else {
		if (!eu_string_ref_ok(name)) {
			codegen_error(codegen, "no implied struct name");
			free(sti);
			return NULL;
		}

		sti->struct_name = name;
	}

	sti->metadata_func_name
		= xsprintf("struct_%.*s_metadata",
			   (int)sti->struct_name.len, sti->struct_name.chars);
	sti->ptr_metadata_func_name
		= xsprintf("struct_%.*s_ptr_metadata",
			   (int)sti->struct_name.len, sti->struct_name.chars);

	sti->descriptor_name
		= xsprintf("struct_%.*s_descriptor",
			   (int)sti->struct_name.len, sti->struct_name.chars);

	sti->extras_type = NULL;
	sti->members = NULL;
	sti->members_len = 0;

	type_info_init(&sti->base, codegen, &struct_type_info_ops,
		       xsprintf("struct_%.*s",
			    (int)sti->struct_name.len, sti->struct_name.chars),
		       1);

	sti->base.c_type_name[REQUIRED]
		= xsprintf("struct %.*s ",
			   (int)sti->struct_name.len, sti->struct_name.chars);
	sti->base.c_type_name[OPTIONAL]
		= xsprintf("struct %.*s *",
			   (int)sti->struct_name.len, sti->struct_name.chars);

	sti->base.metadata_ptr_expr[REQUIRED]
		= xsprintf("%s()", sti->metadata_func_name);
	sti->base.metadata_ptr_expr[OPTIONAL]
		= xsprintf("%s()", sti->ptr_metadata_func_name);

	sti->base.descriptor_ptr_expr[REQUIRED]
		= xsprintf("&%s.struct_base", sti->descriptor_name);
	sti->base.descriptor_ptr_expr[OPTIONAL]
		= xsprintf("&%s.struct_ptr_base", sti->descriptor_name);

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
			struct schema *schema)
{
	struct struct_type_info *sti = (void *)ti;

	if (schema->properties) {
		struct struct_schema_members *props
			= &schema->properties->extras;
		size_t i;

		assert(!sti->members);
		sti->members_len = props->len;
		sti->members = xalloc(sti->members_len * sizeof *sti->members);

		for (i = 0; i < props->len; i++) {
			struct member_info *mi = &sti->members[i];
			struct struct_schema_member *p = &props->members[i];

			mi->json_name = p->name;
			mi->c_name = sanitize_name(mi->json_name);
			mi->type = resolve_type(codegen, &p->value,
						eu_string_ref_null);
		}
	}

	if (schema->additionalProperties)
		sti->extras_type = resolve_type(codegen,
						schema->additionalProperties,
						eu_string_ref_null);
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
				"\treturn eu_value(p, %s());\n"
				"}\n\n",
				sti->ptr_metadata_func_name);

	emit_inlinish_func_decl(codegen,
			"struct eu_value %.*s_to_eu_value(struct %.*s *p)",
			(int)sti->struct_name.len, sti->struct_name.chars,
			(int)sti->struct_name.len, sti->struct_name.chars);
	emit_inlinish_func_body(codegen,
				"{\n"
				"\treturn eu_value(p, %s());\n"
				"}\n\n",
				sti->metadata_func_name);
}

static void struct_define(struct type_info *ti, struct codegen *codegen)
{
	struct struct_type_info *sti = (void *)ti;
	size_t i;
	int presence_count;
	struct type_info *extras_type;
	char *metadata_ptr_name, *ptr_metadata_ptr_name;

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
		if (!mi->type->no_presence_bit)
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
		declare(mi->type, codegen->h_out, mi->c_name, OPTIONAL);
	}

	fprintf(codegen->h_out,
		"\tstruct %s extras;\n"
		"};\n\n",
		extras_type->members_struct_name);

	/* Descriptor definition */

	fprintf(codegen->c_out,
		"static const struct eu_struct_member_descriptor_v1 %.*s_members[%d] = {\n",
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

		if (mi->type->no_presence_bit) {
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
			mi->type->descriptor_ptr_expr[OPTIONAL]);
	}

	fprintf(codegen->c_out, "};\n\n");

	metadata_ptr_name = xsprintf("struct_%.*s_metadata_ptr",
			      (int)sti->struct_name.len, sti->struct_name.chars);
	ptr_metadata_ptr_name = xsprintf("struct_%.*s_ptr_metadata_ptr",
			      (int)sti->struct_name.len, sti->struct_name.chars);

	fprintf(codegen->h_out,
		"extern const struct eu_metadata *%s;\n"
		"extern const struct eu_metadata *%s;\n"
		"extern const struct eu_struct_descriptor_v1 %s;\n\n",
		metadata_ptr_name,
		ptr_metadata_ptr_name,
		sti->descriptor_name);

	fprintf(codegen->c_out,
		"const struct eu_metadata *%s;\n"
		"const struct eu_metadata *%s;\n\n",
		metadata_ptr_name,
		ptr_metadata_ptr_name);

	fprintf(codegen->c_out,
		"const struct eu_struct_descriptor_v1 %s = {\n"
		"\t{ &%s, EU_TDESC_STRUCT_V1 },\n"
		"\t{ &%s, EU_TDESC_STRUCT_PTR_V1 },\n"
		"\tsizeof(struct %.*s),\n"
		"\toffsetof(struct %.*s, extras),\n"
		"\tsizeof(struct %s),\n"
		"\toffsetof(struct %s, value),\n"
		"\tsizeof(%.*s_members) / sizeof(struct eu_struct_member_descriptor_v1),\n"
		"\t%.*s_members,\n"
		"\t%s\n"
		"};\n\n",
		sti->descriptor_name,
		metadata_ptr_name,
		ptr_metadata_ptr_name,
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars,
		extras_type->member_struct_name,
		extras_type->member_struct_name,
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars,
		extras_type->descriptor_ptr_expr[REQUIRED]);

	fprintf(codegen->h_out,
		"static __inline__ const struct eu_metadata *%s(void)\n"
		"{\n"
		"\tif (%s)\n"
		"\t\treturn %s;\n"
		"\telse\n"
		"\t\treturn eu_introduce(&%s.struct_base);\n"
		"}\n\n",
		sti->metadata_func_name,
		metadata_ptr_name, metadata_ptr_name,
		sti->descriptor_name);

	fprintf(codegen->h_out,
		"static __inline__ const struct eu_metadata *%s(void)\n"
		"{\n"
		"\tif (%s)\n"
		"\t\treturn %s;\n"
		"\telse\n"
		"\t\treturn eu_introduce(&%s.struct_ptr_base);\n"
		"}\n\n",
		sti->ptr_metadata_func_name,
		ptr_metadata_ptr_name, ptr_metadata_ptr_name,
		sti->descriptor_name);

	free(metadata_ptr_name);
	free(ptr_metadata_ptr_name);

	fprintf(codegen->h_out,
		"void %.*s_init(struct %.*s *p);\n"
		"void %.*s_fini(struct %.*s *p);\n",
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars);

	fprintf(codegen->c_out,
		"void %.*s_init(struct %.*s *p)\n"
		"{\n"
		"\tmemset(p, 0, sizeof *p);\n"
		"}\n\n",
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
		"\t\teu_struct_extras_fini(%s(), &p->extras);\n"
		"}\n\n",
		sti->metadata_func_name);

	fprintf(codegen->h_out,
		"void %.*s_destroy(struct %.*s *p);\n\n",
		(int)sti->struct_name.len, sti->struct_name.chars,
		(int)sti->struct_name.len, sti->struct_name.chars);

	fprintf(codegen->c_out,
		"void %.*s_destroy(struct %.*s *p)\n"
		"{\n"
		"\tif (p) {\n"
		"\t\t%.*s_fini(p);\n"
		"\t\tfree(p);\n"
		"\t}\n"
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
	free(sti->metadata_func_name);
	free(sti->ptr_metadata_func_name);
	free(sti->descriptor_name);
	free((void *)sti->base.base_name);
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
	struct_define,
	struct_generate_fini,
	struct_destroy
};

/* Arrays */

struct array_type_info {
	struct type_info base;
	char *metadata_func_name;
	char *descriptor_name;
	struct type_info *element_type;
};

static char *string_ref_to_cstr(struct eu_string_ref s)
{
	/* This ought to check that s is a valid C identifier. */
	char *c = xalloc(s.len + 1);
	memcpy(c, s.chars, s.len);
	c[s.len] = 0;
	return c;
}

static struct type_info_ops array_type_info_ops;

static struct type_info *alloc_array(struct schema *schema,
				     struct codegen *codegen,
				     struct eu_string_ref name)
{
	struct array_type_info *ati = xalloc(sizeof *ati);
	char *cname;

	ati->element_type = NULL;

	if (!eu_string_ref_ok(name)) {
		/* This is not safe, as it might be a reference to
		   ourself.  But we need to resolve it early to get
		   the base_name.  Need to detect the circularity case
		   and gensym a name. */
		ati->element_type = resolve_type(codegen,
						 schema->additionalItems,
						 eu_string_ref_null);

		cname = xsprintf("%s_array", ati->element_type->base_name);
	}
	else {
		cname = string_ref_to_cstr(name);
	}

	ati->metadata_func_name = xsprintf("%s_metadata", cname);
	ati->descriptor_name = xsprintf("%s_descriptor", cname);

	type_info_init(&ati->base, codegen, &array_type_info_ops, cname,
		       1);

	ati->base.c_type_name[REQUIRED]
		= ati->base.c_type_name[OPTIONAL]
		= xsprintf("struct %s ", cname);
	ati->base.metadata_ptr_expr[REQUIRED]
		= ati->base.metadata_ptr_expr[OPTIONAL]
		= xsprintf("%s()", ati->metadata_func_name);
	ati->base.descriptor_ptr_expr[REQUIRED]
		= ati->base.descriptor_ptr_expr[OPTIONAL]
		= xsprintf("&%s.base", ati->descriptor_name);

	return &ati->base;
}

static void array_fill(struct type_info *ti, struct codegen *codegen,
		       struct schema *schema)
{
	struct array_type_info *ati = (void *)ti;

	if (!ati->element_type)
		ati->element_type = resolve_type(codegen, schema->additionalItems,
						 eu_string_ref_null);
}

static void array_define(struct type_info *ti, struct codegen *codegen)
{
	struct array_type_info *ati = (void *)ti;
	char *metadata_ptr_name;

	fprintf(codegen->h_out, "struct %s {\n", ti->base_name);
	declare(ati->element_type, codegen->h_out, "*a", REQUIRED);
	fprintf(codegen->h_out,
		"\tsize_t len;\n\n"
		"\tstruct {\n"
		"\t\tsize_t capacity;\n"
		"\t} priv;\n"
		"};\n\n");

	fprintf(codegen->h_out,
		"static __inline__ %s*%s_push(struct %s *array) {\n"
		"\tif (array->len < array->priv.capacity\n"
		"\t    || eu_array_grow((struct eu_array *)array, array->len + 1,\n"
		"\t                     sizeof(*array->a)))\n"
		"\t\treturn array->a + array->len++;\n"
		"\telse\n"
		"\t\treturn NULL;\n"
		"\t}\n\n",
		ati->element_type->c_type_name[REQUIRED],
		ti->base_name,
		ti->base_name);

	/* Descriptor definition */

	metadata_ptr_name = xsprintf("%s_metadata_ptr", ti->base_name);

	fprintf(codegen->h_out,
		"extern const struct eu_metadata *%s;\n"
		"extern const struct eu_array_descriptor_v1 %s;\n\n",
		metadata_ptr_name,
		ati->descriptor_name);

	fprintf(codegen->c_out,
		"const struct eu_metadata *%s;\n\n"
		"const struct eu_array_descriptor_v1 %s = {\n"
		"\t{ &%s, EU_TDESC_ARRAY_V1 },\n"
		"\t%s\n"
		"};\n\n",
		metadata_ptr_name,
		ati->descriptor_name,
		metadata_ptr_name,
		ati->element_type->descriptor_ptr_expr[REQUIRED]);

	fprintf(codegen->h_out,
		"static __inline__ const struct eu_metadata *%s(void)\n"
		"{\n"
		"\tif (%s)\n"
		"\t\treturn %s;\n"
		"\telse\n"
		"\t\treturn eu_introduce(&%s.base);\n"
		"}\n\n",
		ati->metadata_func_name,
		metadata_ptr_name, metadata_ptr_name,
		ati->descriptor_name);

	free(metadata_ptr_name);
}

static void array_call_fini(struct type_info *ti, FILE *out, const char *var_expr)
{
	fprintf(out, "\teu_array_fini(%s, &%s);\n", ti->metadata_ptr_expr[REQUIRED], var_expr);
}

static void array_destroy(struct type_info *ti)
{
	struct array_type_info *ati = (void *)ti;

	type_info_fini(ti);
	free(ati->metadata_func_name);
	free(ati->descriptor_name);
	free((void *)ati->base.base_name);
	free(ati);
}

static struct type_info_ops array_type_info_ops = {
	array_fill,
	array_define,
	array_call_fini,
	array_destroy
};


/* Definition resolution */

#define REF_PREFIX "#/definitions/"
#define REF_PREFIX_LEN 14

static struct definition *find_def(struct codegen *codegen,
				   struct eu_string_ref ref)
{
	size_t i;

	if (ref.len < REF_PREFIX_LEN
	    || memcmp(ref.chars, REF_PREFIX, REF_PREFIX_LEN)) {
		codegen_error(codegen,
		    "only refs beginning with '" REF_PREFIX "' are supported");
		return NULL;
	}

	ref.chars += REF_PREFIX_LEN;
	ref.len -= REF_PREFIX_LEN;

	for (i = 0; i < codegen->n_defs; i++) {
		struct definition *def = &codegen->defs[i];

		if (eu_string_ref_equal(def->name, ref))
			return def;
	}

	codegen_error(codegen, "unknown definition \"%.*s\"",
		      (int)ref.len, ref.chars);
	return NULL;
}

static struct type_info *resolve_ref(struct codegen *codegen,
				     struct eu_string_ref ref)
{
	struct definition *def = find_def(codegen, ref);
	if (def)
		return def->u.type;
	else
		return NULL;

}

static int is_empty_schema(struct schema *schema)
{
	return !schema->ref.chars
		&& !schema->type.chars
		&& !schema->additionalProperties
		&& !schema->euphemusStructName.chars
		&& !schema->extras.len;
}

static struct type_info *alloc_type(struct codegen *codegen,
				    struct schema *schema,
				    struct eu_string_ref name)
{
	struct eu_string_ref type;
	struct type_info *res;

	if (!schema->type.chars) {
		if (is_empty_schema(schema))
			return codegen->variant_type;

		codegen_error(codegen,
			  "schema lacks a \"type\" but is not an empty schema");
		return NULL;
	}

	type = eu_string_to_ref(&schema->type);

	if (eu_string_ref_equal(type, eu_cstr("object")))
		return alloc_struct(schema, codegen, name);
	else if (eu_string_ref_equal(type, eu_cstr("array")))
		return alloc_array(schema, codegen, name);
	else if (eu_string_ref_equal(type, eu_cstr("string")))
		res = codegen->string_type;
	else if (eu_string_ref_equal(type, eu_cstr("number")))
		res = codegen->number_type;
	else if (eu_string_ref_equal(type, eu_cstr("boolean")))
		res = codegen->bool_type;
	else {
		codegen_error(codegen, "unknown type \"%.*s\"",
			      (int)type.len, type.chars);
		return NULL;
	}

	if (eu_object_size(schema_to_eu_value(schema)) != 1) {
		codegen_error(codegen,
			      "unexpected members in schema of type \"%.*s\"",
			      (int)type.len, type.chars);
		return NULL;
	}

	return res;
}

static struct type_info *resolve_type(struct codegen *codegen,
				      struct schema *schema,
				      struct eu_string_ref name_hint)
{
	struct type_info *ti;

	if (schema->ref.chars) {
		if (eu_object_size(schema_to_eu_value(schema)) == 1)
			return resolve_ref(codegen,
					   eu_string_to_ref(&schema->ref));

		codegen_error(codegen,
			      "\"$ref\" object contains other members");
		return NULL;
	}

	ti = alloc_type(codegen, schema, name_hint);
	if (ti)
		fill_type(ti, codegen, schema);

	return ti;
}

static void definition_set_error(struct definition *def)
{
	def->state = DEF_ERROR;
	def->u.type = NULL;
}

static struct type_info *alloc_definition(struct codegen *codegen,
					  struct definition *def)
{
	switch (def->state) {
	case DEF_SCHEMA:
		def->u.type = alloc_type(codegen, def->u.schema, def->name);
		def->state = DEF_SCHEMA_TYPE;
		break;

	case DEF_REF:
		def->state = DEF_RESOLVING;
		if ((def->u.type = alloc_definition(codegen, def->u.ref)))
			def->state = DEF_TYPE;
		else
			definition_set_error(def);

		break;

	case DEF_RESOLVING:
		codegen_error(codegen, "circular reference");
		return NULL;

	case DEF_SCHEMA_TYPE:
	case DEF_TYPE:
	case DEF_ERROR:
		break;

	default:
		abort();
	}

	return def->u.type;
}

static void codegen_process_definitions(struct codegen *codegen,
					struct named_schemas *definitions)
{
	size_t i;
	size_t n = definitions->extras.len;

	codegen->n_defs = n;
	codegen->defs = xalloc(n * sizeof *codegen->defs);

	/* Fill in name field of definitions so that find_def works. */
	for (i = 0; i < n; i++)
		codegen->defs[i].name = definitions->extras.members[i].name;

	/* Identify definitions which directly contain a reference,
	   and the target definitions in such cases. */
	for (i = 0; i < n; i++) {
		struct definition *def = &codegen->defs[i];
		struct schema *schema = &definitions->extras.members[i].value;

		if (!schema->ref.chars) {
			def->state = DEF_SCHEMA;
			def->u.schema = schema;
		}
		else if (eu_object_size(schema_to_eu_value(schema)) == 1) {
			def->state = DEF_REF;
			if (!(def->u.ref = find_def(codegen,
					      eu_string_to_ref(&schema->ref))))
				definition_set_error(def);
		}
		else {
			codegen_error(codegen,
				      "\"$ref\" object contains other members");
			definition_set_error(def);
		}
	}

	/* Allocate all definition schemas and resolve definitions
	   which directly contain a reference. */
	for (i = 0; i < n; i++)
		alloc_definition(codegen, &codegen->defs[i]);

	/* Fill any non-ref definition schemas */
	for (i = 0; i < n; i++) {
		struct definition *def = &codegen->defs[i];

		if (def->state == DEF_SCHEMA_TYPE)
			fill_type(def->u.type, codegen,
				  &definitions->extras.members[i].value);
	}
}

/* Produce definitions for definitions */
static void codegen_define_definitions(struct codegen *codegen)
{
	size_t i;

	for (i = 0; i < codegen->n_defs; i++) {
		struct definition *def = &codegen->defs[i];
		if (def->state == DEF_SCHEMA_TYPE)
			define_type(def->u.type, codegen);
	}
}

static void codegen_prolog(struct codegen *codegen)
{
	char *hp;

	fprintf(codegen->h_out,
		"/* Generated from \"%s\".  You probably shouldn't edit this file. */\n\n"
		"#include <limits.h>\n"
		"#include <euphemus.h>\n\n",
		codegen->source_path);

	hp = xstrdup(codegen->h_out_path);
	fprintf(codegen->c_out,
		"/* Generated from \"%s\".  You probably shouldn't edit this file. */\n\n"
		"#include <stddef.h>\n\n"
		"#include \"%s\"\n\n",
		codegen->source_path, basename(hp));
	free(hp);
}

static void do_codegen(struct codegen *codegen, struct schema *schema)
{
	struct type_info *main_type;
	char *tmp, *schema_name;

	tmp = xstrdup(codegen->source_path);
	schema_name = remove_extension(basename(tmp));
	free(tmp);

	/* Process type definitions */
	codegen_process_definitions(codegen, schema->definitions);
	main_type = resolve_type(codegen, schema, eu_cstr(schema_name));
	if (codegen->error_count)
		return;

	/* Prepare to generate code. */
	codegen_open_output_files(codegen);
	if (codegen->error_count)
		return;

	codegen_prolog(codegen);

	/* Actually generate code */
	define_type(main_type, codegen);
	codegen_define_definitions(codegen);

	free(schema_name);
}

static void parse_schema_file(struct codegen *codegen, struct eu_value dest)
{
	struct eu_parse *parse;
	FILE *fp = fopen(codegen->source_path, "r");

	if (!fp) {
		codegen_error(codegen, "%s",strerror(errno));
		return;
	}

	parse = eu_parse_create(dest);

	while (!feof(fp)) {
		char buf[1000];
		size_t got = fread(buf, 1, 1000, fp);

		if (got < 1000 && ferror(fp)) {
			codegen_error(codegen, "%s",strerror(errno));
			goto out;
		}

		if (!eu_parse(parse, buf, got)) {
			codegen_error(codegen, "parse error");
			goto out;
		}
	}

	if (!eu_parse_finish(parse))
		codegen_error(codegen, "parse error");

 out:
	fclose(fp);
	eu_parse_destroy(parse);
}

int main(int argc, char **argv)
{
	int i;
	struct codegen codegen;
	int all_ok = 1;

	for (i = 1; i < argc; i++) {
		struct schema schema;

		codegen_init(&codegen, argv[i]);

		parse_schema_file(&codegen, schema_to_eu_value(&schema));
		if (!codegen.error_count) {
			do_codegen(&codegen, &schema);
			schema_fini(&schema);
		}

		if (codegen.error_count) {
			codegen_delete_output_files(&codegen);
			all_ok = 0;
		}

		codegen_fini(&codegen);
	}

	return !all_ok;
}
