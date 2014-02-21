#ifndef EUPHEMUS_UNESCAPE_H
#define EUPHEMUS_UNESCAPE_H

typedef uint16_t eu_unescape_state_t;
char *eu_unescape(struct eu_parse *ep, const char *end, char *dest,
		  eu_unescape_state_t *ues);
int eu_finish_unescape(struct eu_parse *ep, eu_unescape_state_t *ues,
		       char *out);

/* Determine whether a double-quotes character was it escaped, by
   scanning backwards counting backslashes.  This function should be
   called with a double-quotes character preceding 'p', so we don't
   need to check for running off the start of the buffer. */
static __inline__ int quotes_escaped(const char *p)
{
	size_t backslashes = 0;

	while (*--p == '\\')
		backslashes++;

	/* The double-quotes is escaped if preceded by an odd number
	   of backslashes. */
	return (backslashes & 1);
}

/* Determine whether a double-quotes character was it escaped, by
   scanning backwards counting backslashes.  'start' marks the start
   of available character, and should not be inside an esape
   sequence. */
static __inline__ int quotes_escaped_bounded(const char *p, const char *start)
{
	size_t backslashes = 0;

	while (p != start && *--p == '\\')
		backslashes++;

	/* The double-quotes is escaped if preceded by an odd number
	   of backslashes. */
	return (backslashes & 1);
}

#endif
