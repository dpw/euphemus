#include "euphemus.h"

/* This implements JSON Pointer paths, but without the ~ escaping yet. */
int eu_resolve_path(struct eu_value *val, struct eu_string_ref path)
{
	const char *end, *p = path.chars;

	if (path.len == 0)
		/* An empty path means the whole document */
		return 1;

	if (*p != '/')
		/* All non-empty paths should start with '/' */
		return 0;

	end = path.chars + path.len;
	for (path.chars = ++p; p != end;) {
		if (*p != '/') {
			p++;
			continue;
		}

		path.len = p - path.chars;
		if (!val->metadata->resolve(val, path))
			return 0;

		path.chars = ++p;
	}

	path.len = p - path.chars;
	return val->metadata->resolve(val, path);
}

int eu_resolve_error(struct eu_value *val, struct eu_string_ref name)
{
	(void)val;
	(void)name;
	return 0;
}

