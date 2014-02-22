#include <limits.h>

#include <euphemus.h>
#include "euphemus_int.h"
#include "unescape.h"

/* This magic relies on the coincidence that 'b' - '\b' == 'f' - '\f',
   'b' and 'f' being the only two escape chars that are also hex
   digits. */

#define HEX_XOR ('b' - 8)
#define ESCAPE_XOR (('b' - 0xb) | 0x80)
#define HEX_CHAR(c, x) [c] = ((c - x) ^ HEX_XOR)
#define ESCAPE_CHAR(c, e) [c] = ((c - e) ^ ESCAPE_XOR)
#define HEX_AND_ESCAPE_CHAR_VAL(c, x, e) (((c - x) ^ (c - e)) | 0x80)
#define HEX_AND_ESCAPE_CHAR(c, x, e) [c] = HEX_AND_ESCAPE_CHAR_VAL(c, x, e)

STATIC_ASSERT(HEX_AND_ESCAPE_CHAR_VAL('b', 0xb, 8)
	      == HEX_AND_ESCAPE_CHAR_VAL('f', 0xf, 12));

static char unescape_table[UCHAR_MAX] CACHE_ALIGN = {
	ESCAPE_CHAR('"', '"'),
	ESCAPE_CHAR('\\', '\\'),
	ESCAPE_CHAR('/', '/'),

	ESCAPE_CHAR('n', 10),
	ESCAPE_CHAR('r', 13),
	ESCAPE_CHAR('t', 9),

	HEX_CHAR('0', 0),
	HEX_CHAR('1', 1),
	HEX_CHAR('2', 2),
	HEX_CHAR('3', 3),
	HEX_CHAR('4', 4),
	HEX_CHAR('5', 5),
	HEX_CHAR('6', 6),
	HEX_CHAR('7', 7),
	HEX_CHAR('8', 8),
	HEX_CHAR('9', 9),

	HEX_CHAR('A', 0xA),
	HEX_CHAR('B', 0xB),
	HEX_CHAR('C', 0xC),
	HEX_CHAR('D', 0xD),
	HEX_CHAR('E', 0xE),
	HEX_CHAR('F', 0xF),

	HEX_CHAR('a', 0xa),
	HEX_AND_ESCAPE_CHAR('b', 0xb, 8),
	HEX_CHAR('c', 0xc),
	HEX_CHAR('d', 0xa),
	HEX_CHAR('e', 0xa),
	HEX_AND_ESCAPE_CHAR('f', 0xf, 12),
};

enum ues_state {
	UES_BACKSLASH = 1,
	UES_U,
	UES_UX,
	UES_UXX,
	UES_UXXX
};

char *eu_unicode_to_utf8(eu_unicode_char_t uc, char *dest)
{
	if (uc < 0x80) {
		*dest++ = uc;
	}
	else {
		if (uc < 0x800) {
			*dest++ = uc >> 6 | 0xc0;
		}
		else {
			if (uc < 0x10000) {
				*dest++ = uc >> 12 | 0xe0;
			}
			else {
				*dest++ = uc >> 18 | 0xf0;
				*dest++ = (uc >> 12 & 0x3f) | 0x80;
			}

			*dest++ = (uc >> 6 & 0x3f) | 0x80;
		}

		*dest++ = (uc & 0x3f) | 0x80;
	}

	return dest;
}

int eu_unicode_utf8_length(eu_unicode_char_t uc)
{
	if (uc < 0x80)
		return 1;
	else if (uc < 0x800)
		return 2;
	else if (uc < 0x10000)
		return 3;
	else
		return 4;
}

/* Copy characters from ep->input to dest, unescaping any escape sequences
 * encountered.  If the input ends with an incomplete escape sequence,
 * the state information is placed in ues. Returns the end of the
 * output characters, or NULL on error. */
char *eu_unescape(struct eu_parse *ep, const char *end, char *dest,
		  eu_unescape_state_t *ues_out)
{
	const char *p = ep->input;
	eu_unescape_state_t ues = 0;
	enum ues_state state;

	while (p != end) {
		char c, magic, unescaped;

		c = *p++;
		if (c != '\\') {
			*dest++ = c;
			continue;
		}

		state = UES_BACKSLASH;
		if (p == end)
			goto pause;

		c = *p++;
		magic = unescape_table[(unsigned char)c];
		unescaped = c - (magic ^ ESCAPE_XOR);
		if ((magic & 0x80)) {
			*dest++ = unescaped;
			continue;
		}

		if (c != 'u')
			goto bad_escape;

		/* In a \u sequence */
		for (;;) {
			state++;
			if (p == end)
				goto pause;

			c = *p++;
			magic = unescape_table[(unsigned char)c];
			c -= (magic & 0x7f) ^ HEX_XOR;
			if (!(magic & 0x08))
				goto bad_escape;

			if (state == UES_UXXX)
				break;

			ues |= (eu_unescape_state_t)c << (UES_UXXX - state) * 4;
		}

		dest = eu_unicode_to_utf8(ues | c, dest);
		ues = 0;
	}

	state = 0;
 pause:
	*ues_out = ues | state;
	return dest;

 bad_escape:
	ep->input = p;
	return NULL;
}

int eu_finish_unescape(struct eu_parse *ep, eu_unescape_state_t *ues_io,
		       eu_unicode_char_t *out)
{
	const char *p = ep->input;
	const char *end = ep->input_end;
	eu_unescape_state_t ues = *ues_io;
	enum ues_state state = ues & 0xf;
	char c, magic;

	*ues_io = 0;

	switch (state) {
	case UES_BACKSLASH:
		if (p == end)
			goto pause;

		c = *p++;
		magic = unescape_table[(unsigned char)c];
		*out = (char)(c - (magic ^ ESCAPE_XOR));
		if ((magic & 0x80))
			goto done;

		if (c != 'u')
			goto bad_escape;

		state = UES_U;
		/* fall through */

	default:
		/* In a \u sequence */
		ues &= ~0xf;

		for (;;) {
			if (p == end)
				goto pause;

			c = *p++;
			magic = unescape_table[(unsigned char)c];
			c -= (magic & 0x7f) ^ HEX_XOR;
			if (!(magic & 0x08))
				goto bad_escape;

			if (state == UES_UXXX)
				break;

			ues |= (eu_unescape_state_t)c << (UES_UXXX - state) * 4;
			state++;
		}

		*out = ues | c;
		goto done;
	}

 pause:
	*ues_io = ues | state;
 done:
	ep->input = p;
	return 1;

 bad_escape:
	ep->input = p;
	return 0;
}
