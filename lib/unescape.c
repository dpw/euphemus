#include <limits.h>

#include <euphemus.h>
#include "euphemus_int.h"
#include "unescape.h"

static char unescape_table[UCHAR_MAX] CACHE_ALIGN = {
	['"'] = '\"',
	['\\'] = '\\',
	['/'] = '/',
	['b'] = '\b',
	['f'] = '\f',
	['n'] = '\n',
	['r'] = '\r',
	['t'] = '\t'
};

/* Copy characters from ep->input to dest, unescaping any escape sequences
 * encountered.  If the input ends with an incomplete escape sequence,
 * the state information is placed in ues. Returns the end of the
 * output characters, or NULL on error. */
char *eu_unescape(struct eu_parse *ep, const char *end, char *dest,
		  eu_unescape_state_t *ues)
{
	const char *p = ep->input;

	while (p != end) {
		if (*p != '\\') {
			*dest++ = *p++;
			continue;
		}

		if (++p != end) {
			char unescaped = unescape_table[(unsigned char)*p++];
			if (unescaped)
				*dest++ = unescaped;
			else
				/* bad escape sequence */
				return NULL;
		}
		else {
			*ues = 1;
			return dest;
		}
	}

	*ues = 0;
	return dest;
}

int eu_finish_unescape(struct eu_parse *ep, eu_unescape_state_t *ues,
		       char *out)
{
	if (ep->input != ep->input_end) {
		char unescaped = unescape_table[(unsigned char)*ep->input];
		if (!unescaped)
			/* bad escape sequence */
			return 0;

		*out = unescaped;
		*ues = 0;
		ep->input++;
	}
	else {
		*ues = 1;
	}

	return 1;
}
