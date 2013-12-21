#include "euphemus.h"
#include "euphemus_int.h"

enum eu_resolve_result eu_resolve(struct eu_value *val,
				  struct eu_string_value *path, size_t len)
{
	size_t i;
	enum eu_resolve_result res = EU_RESOLVE_OK;

	for (i = 0; i < len; i++) {
		res = val->metadata->resolve(val, path[i]);
		if (res != EU_RESOLVE_OK)
			break;
	}

	return res;
}

enum eu_resolve_result eu_resolve_error(struct eu_value *val,
					struct eu_string_value name)
{
	(void)val;
	(void)name;
	return EU_RESOLVE_ERROR;
}

