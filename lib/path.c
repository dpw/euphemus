#include <euphemus.h>

/* This implements JSON Pointer paths, but without the ~ escaping yet. */
struct eu_value eu_get_path(struct eu_value val, struct eu_string_ref path)
{
	const char *end, *p = path.chars;

	if (path.len == 0)
		/* An empty path means the whole document */
		return val;

	if (*p != '/')
		/* All non-empty paths should start with '/' */
		return eu_value_none;

	end = path.chars + path.len;
	for (path.chars = ++p; p != end;) {
		if (*p != '/') {
			p++;
			continue;
		}

		path.len = p - path.chars;
		val = val.metadata->get(val, path);
		if (!eu_value_ok(val))
			return val;

		path.chars = ++p;
	}

	path.len = p - path.chars;
	return val.metadata->get(val, path);
}

struct eu_value eu_get_error(struct eu_value val, struct eu_string_ref name)
{
	(void)val;
	(void)name;
	return eu_value_none;
}
