/* Generated from "schemac/schema_schema.json".  You probably shouldn't edit this file. */

#include <limits.h>
#include <euphemus.h>

#ifndef STRUCT_SCHEMA_MEMBERS_DEFINED
#define STRUCT_SCHEMA_MEMBERS_DEFINED
struct struct_schema_member {
	struct eu_string_ref name;
	struct schema *value;
};

struct struct_schema_members {
	struct struct_schema_member *members;
	size_t len;
	struct {
		size_t capacity;
	} priv;
};
#endif

struct named_schemas {
	struct struct_schema_members extras;
};

extern const struct eu_metadata *struct_named_schemas_metadata_ptr;
extern const struct eu_metadata *struct_named_schemas_ptr_metadata_ptr;
extern const struct eu_struct_descriptor_v1 struct_named_schemas_descriptor;

static __inline__ const struct eu_metadata *struct_named_schemas_metadata(void)
{
	if (struct_named_schemas_metadata_ptr)
		return struct_named_schemas_metadata_ptr;
	else
		return eu_introduce(&struct_named_schemas_descriptor.struct_base);
}

static __inline__ const struct eu_metadata *struct_named_schemas_ptr_metadata(void)
{
	if (struct_named_schemas_ptr_metadata_ptr)
		return struct_named_schemas_ptr_metadata_ptr;
	else
		return eu_introduce(&struct_named_schemas_descriptor.struct_ptr_base);
}

void named_schemas_fini(struct named_schemas *p);
void named_schemas_destroy(struct named_schemas *p);

static __inline__ struct eu_value named_schemas_ptr_to_eu_value(struct named_schemas **p)
{
	return eu_value(p, struct_named_schemas_ptr_metadata());
}

static __inline__ struct eu_value named_schemas_to_eu_value(struct named_schemas *p)
{
	return eu_value(p, struct_named_schemas_metadata());
}

#ifndef EU_VARIANT_MEMBERS_DEFINED
#define EU_VARIANT_MEMBERS_DEFINED
struct eu_variant_member {
	struct eu_string_ref name;
	struct eu_variant value;
};

struct eu_variant_members {
	struct eu_variant_member *members;
	size_t len;
	struct {
		size_t capacity;
	} priv;
};
#endif

struct schema {
	unsigned char presence_bits[(4 - 1) / CHAR_BIT + 1];
	struct eu_string ref;
	struct named_schemas *definitions;
	struct eu_string type;
	struct eu_string title;
	struct named_schemas *properties;
	struct schema *additionalProperties;
	struct schema *additionalItems;
	struct eu_string euphemusStructName;
	struct eu_variant_members extras;
};

extern const struct eu_metadata *struct_schema_metadata_ptr;
extern const struct eu_metadata *struct_schema_ptr_metadata_ptr;
extern const struct eu_struct_descriptor_v1 struct_schema_descriptor;

static __inline__ const struct eu_metadata *struct_schema_metadata(void)
{
	if (struct_schema_metadata_ptr)
		return struct_schema_metadata_ptr;
	else
		return eu_introduce(&struct_schema_descriptor.struct_base);
}

static __inline__ const struct eu_metadata *struct_schema_ptr_metadata(void)
{
	if (struct_schema_ptr_metadata_ptr)
		return struct_schema_ptr_metadata_ptr;
	else
		return eu_introduce(&struct_schema_descriptor.struct_ptr_base);
}

void schema_fini(struct schema *p);
void schema_destroy(struct schema *p);

static __inline__ struct eu_value schema_ptr_to_eu_value(struct schema **p)
{
	return eu_value(p, struct_schema_ptr_metadata());
}

static __inline__ struct eu_value schema_to_eu_value(struct schema *p)
{
	return eu_value(p, struct_schema_metadata());
}

