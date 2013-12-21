#include "euphemus.h"

int eu_resolve(struct eu_value *val, struct eu_string_value *path, size_t len)
{
	size_t i;
	int res = 1;

	for (i = 0; i < len; i++) {
		res = val->metadata->resolve(val, path[i]);
		if (!res)
			break;
	}

	return res;
}

int eu_resolve_error(struct eu_value *val, struct eu_string_value name)
{
	(void)val;
	(void)name;
	return 0;
}

