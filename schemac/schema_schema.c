/* Generated from "schemac/schema_schema.json".  You probably shouldn't edit this file. */

#include <stddef.h>

#include "schema_schema.h"

static const struct eu_struct_member_descriptor_v1 named_schemas_members[0] = {
};

const struct eu_metadata *struct_named_schemas_metadata_ptr;
const struct eu_metadata *struct_named_schemas_ptr_metadata_ptr;

const struct eu_struct_descriptor_v1 struct_named_schemas_descriptor = {
	{ &struct_named_schemas_metadata_ptr, EU_TDESC_STRUCT_V1 },
	{ &struct_named_schemas_ptr_metadata_ptr, EU_TDESC_STRUCT_PTR_V1 },
	sizeof(struct named_schemas),
	offsetof(struct named_schemas, extras),
	sizeof(struct struct_schema_member),
	offsetof(struct struct_schema_member, value),
	sizeof(named_schemas_members) / sizeof(struct eu_struct_member_descriptor_v1),
	named_schemas_members,
	&struct_schema_descriptor.struct_ptr_base
};

void named_schemas_fini(struct named_schemas *p)
{
	if (p->extras.len)
		eu_struct_extras_fini(struct_named_schemas_metadata(), &p->extras);
}

void named_schemas_destroy(struct named_schemas *p)
{
	if (p) {
		named_schemas_fini(p);
		free(p);
	}
}

static const struct eu_struct_member_descriptor_v1 schema_members[7] = {
	{
		offsetof(struct schema, ref),
		4,
		0 / CHAR_BIT, 1 << (0 % CHAR_BIT),
		"$ref",
		&eu_string_descriptor
	},
	{
		offsetof(struct schema, definitions),
		11,
		-1, 0,
		"definitions",
		&struct_named_schemas_descriptor.struct_ptr_base
	},
	{
		offsetof(struct schema, type),
		4,
		1 / CHAR_BIT, 1 << (1 % CHAR_BIT),
		"type",
		&eu_string_descriptor
	},
	{
		offsetof(struct schema, title),
		5,
		2 / CHAR_BIT, 1 << (2 % CHAR_BIT),
		"title",
		&eu_string_descriptor
	},
	{
		offsetof(struct schema, properties),
		10,
		-1, 0,
		"properties",
		&struct_named_schemas_descriptor.struct_ptr_base
	},
	{
		offsetof(struct schema, additionalProperties),
		20,
		-1, 0,
		"additionalProperties",
		&struct_schema_descriptor.struct_ptr_base
	},
	{
		offsetof(struct schema, euphemusStructName),
		18,
		3 / CHAR_BIT, 1 << (3 % CHAR_BIT),
		"euphemusStructName",
		&eu_string_descriptor
	},
};

const struct eu_metadata *struct_schema_metadata_ptr;
const struct eu_metadata *struct_schema_ptr_metadata_ptr;

const struct eu_struct_descriptor_v1 struct_schema_descriptor = {
	{ &struct_schema_metadata_ptr, EU_TDESC_STRUCT_V1 },
	{ &struct_schema_ptr_metadata_ptr, EU_TDESC_STRUCT_PTR_V1 },
	sizeof(struct schema),
	offsetof(struct schema, extras),
	sizeof(struct eu_variant_member),
	offsetof(struct eu_variant_member, value),
	sizeof(schema_members) / sizeof(struct eu_struct_member_descriptor_v1),
	schema_members,
	&eu_variant_descriptor
};

void schema_fini(struct schema *p)
{
	eu_string_fini(&p->ref);
	if (p->definitions) named_schemas_destroy(p->definitions);
	eu_string_fini(&p->type);
	eu_string_fini(&p->title);
	if (p->properties) named_schemas_destroy(p->properties);
	if (p->additionalProperties) schema_destroy(p->additionalProperties);
	eu_string_fini(&p->euphemusStructName);
	if (p->extras.len)
		eu_struct_extras_fini(struct_schema_metadata(), &p->extras);
}

void schema_destroy(struct schema *p)
{
	if (p) {
		schema_fini(p);
		free(p);
	}
}

