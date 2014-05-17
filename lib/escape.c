#include <euphemus.h>

#include "euphemus_int.h"

struct escape {
	unsigned char ch;
	char escape;
};

static const struct escape escape_table[32] CACHE_ALIGN = {
	{ 1, 0 }, { 0, 0 }, { '\"', '\"' }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 8, 'b' }, { 9, 't' }, { 10, 'n' }, { 0, 0 },
	{ 12, 'f' }, { 13, 'r' }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ '\\', '\\' }, { 0, 0 }, { 0, 0 }, { 0, 0 }
};

struct escape_frame {
	struct eu_stack_frame base;
	struct eu_string_ref str;
};

struct escaping_frame {
	struct eu_stack_frame base;
	struct eu_string_ref str;
	unsigned char pos;
	unsigned char len;
	char buf[6];
};

static enum eu_result escape_resume(struct eu_stack_frame *gframe, void *eg);
static enum eu_result escaping_resume(struct eu_stack_frame *gframe, void *eg);

enum eu_result eu_escape(struct eu_generate *eg, struct eu_string_ref str)
{
	struct escaping_frame *frame;
	size_t len = str.len;
	size_t space;
	const char *in = str.chars;
	const char *in_end;
	char *out = eg->output;
	char *out_end = eg->output_end;
	unsigned char ch;

	while (len) {
		space = out_end - out;
		in_end = in + len;
		if (space < len) {
			if (!space)
				goto pause;

			in_end = in + space;
		}

		do {
			ch = *in++;
			len--;
			if (ch < 32 || ch == '\"' || ch == '\\')
				goto escape;

			*out++ = ch;
		} while (in != in_end);

		continue;

	escape:
		*out++ = '\\';
		if (escape_table[ch & 31].ch == ch) {
			/* A single character escape sequence */
			if (out != out_end) {
				*out++ = escape_table[ch & 31].escape;
			}
			else {
				frame = eu_stack_alloc_first(&eg->stack,
							     sizeof *frame);
				if (!frame)
					goto alloc_error;

				frame->buf[0] = escape_table[ch & 31].escape;
				frame->len = 1;
				goto pause_escaping;
			}
		}
		else {
			/* A \uXXXX escape sequence */
			if (out_end - out >= 5) {
				*out++ = 'u';
				*out++ = '0';
				*out++ = '0';
				*out++ = '0' + ((ch & 0xf0) != 0);
				ch &= 0xf;
				*out++ = (ch < 10 ? '0' : 'a'-10) + ch;
			}
			else {
				char *buf;

				frame = eu_stack_alloc_first(&eg->stack,
							     sizeof *frame);
				if (!frame)
					goto alloc_error;

				buf = frame->buf;

				if (out == out_end)
					goto buf_0;

				*out++ = 'u';
				if (out == out_end)
					goto buf_1;

				*out++ = '0';
				if (out == out_end)
					goto buf_2;

				*out++ = '0';
				if (out == out_end)
					goto buf_3;

				*out++ = '0' + ((ch & 0xf0) != 0);
				goto buf_4;

			buf_0:
				*buf++ = 'u';
			buf_1:
				*buf++ = '0';
			buf_2:
				*buf++ = '0';
			buf_3:
				*buf++ = '0' + ((ch & 0xf0) != 0);
			buf_4:
				ch &= 0xf;
				*buf++ = (ch < 10 ? '0' : 'a'-10) + ch;
				frame->len = buf - frame->buf;
				goto pause_escaping;
			}
		}
	}

	if (out != out_end) {
		*out++ = '\"';
		eg->output = out;
		return EU_OK;
	}

 pause:
	{
		struct escape_frame *frame
			= eu_stack_alloc_first(&eg->stack, sizeof *frame);
		frame->base.resume = escape_resume;
		frame->base.destroy = eu_stack_frame_noop_destroy;
		frame->str.chars = in;
		frame->str.len = len;
		eg->output = out;
		return EU_PAUSED;
	}

 pause_escaping:
	frame->base.resume = escaping_resume;
	frame->base.destroy = eu_stack_frame_noop_destroy;
	frame->str.chars = in;
	frame->str.len = len;
	frame->pos = 0;
	eg->output = out;
	return EU_PAUSED;

 alloc_error:
	eg->output = out;
	return EU_ERROR;
}

static enum eu_result escape_resume(struct eu_stack_frame *gframe, void *eg)
{
	struct escape_frame *frame = (struct escape_frame *)gframe;
	return eu_escape(eg, frame->str);
}

static enum eu_result escaping_resume(struct eu_stack_frame *gframe, void *v_eg)
{
	struct escaping_frame *frame = (struct escaping_frame *)gframe;
	struct eu_generate *eg = v_eg;
	size_t space = eg->output_end - eg->output;

	if (space >= frame->len) {
		memcpy(eg->output, frame->buf + frame->pos, frame->len);
		eg->output += frame->len;
		return eu_escape(eg, frame->str);
	}
	else {
		memcpy(eg->output, frame->buf + frame->pos, space);
		frame->pos += space;
		frame->len -= space;
		eg->output = eg->output_end;
		return EU_REINSTATE_PAUSED;
	}
}
