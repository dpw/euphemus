#include <stdlib.h>

#include <euphemus.h>
#include "euphemus_int.h"

STATIC_ASSERT((sizeof(void *) & (sizeof(void *) - 1)) == 0);

/* Round up to a multiple of sizeof(void *).  This ought to be
 * sufficient to suitably align all stack frames. */
#define ROUND_UP(n) ((((n) - 1) & -sizeof(void *)) + sizeof(void *))

void *eu_stack_init(struct eu_stack *st, size_t alloc_size)
{
	void *stack = malloc(alloc_size);

	if (stack) {
		st->stack = stack;
		st->stack_area_size = alloc_size;
		st->scratch_size = st->new_stack_top = st->new_stack_bottom
			= st->old_stack_bottom = 0;

		((struct eu_stack_frame *)stack)->size = alloc_size;

		return stack;
	}

	return NULL;
}

void eu_stack_begin_pause(struct eu_stack *st)
{
	if (st->new_stack_top != st->new_stack_bottom) {
		/* There is a new stack from a previous pause.
		   Consolidate it with the old stack. */
		size_t new_stack_size
			= st->new_stack_top - st->new_stack_bottom;
		st->old_stack_bottom -= new_stack_size;
		memmove(st->stack + st->old_stack_bottom,
			st->stack + st->new_stack_bottom,
			new_stack_size);
	}

	st->new_stack_bottom = st->new_stack_top = ROUND_UP(st->scratch_size);
}

void *eu_stack_alloc(struct eu_stack *st, size_t size)
{
	size_t new_stack_top;
	struct eu_stack_frame *f;

	size = ROUND_UP(size);
	new_stack_top = st->new_stack_top + size;

	/* Do we have space for the new stack frame */
	if (unlikely(new_stack_top > st->old_stack_bottom)) {
		/* Need to expand the stack area, creating a bigger
		   gap between the new and old stack regions. */
		size_t old_stack_size
			= st->stack_area_size - st->old_stack_bottom;
		char *stack;

		do {
			st->stack_area_size *= 2;
		} while (st->stack_area_size < new_stack_top + old_stack_size);

		stack = malloc(st->stack_area_size);
		if (stack == NULL)
			return NULL;

		memcpy(stack, st->stack, st->new_stack_top);
		memcpy(stack + st->stack_area_size - old_stack_size,
		       st->stack + st->old_stack_bottom, old_stack_size);
		free(st->stack);

		st->stack = stack;
		st->old_stack_bottom = st->stack_area_size - old_stack_size;
	}

	f = (struct eu_stack_frame *)(st->stack + st->new_stack_top);
	f->size = size;
	st->new_stack_top = new_stack_top;
	return f;
}

int eu_stack_run(struct eu_stack *st, void *context)
{
	struct eu_stack_frame *f;

	/* Process frames from the new stack */
	while (st->new_stack_bottom != st->new_stack_top) {
		f = (struct eu_stack_frame *)(st->stack + st->new_stack_bottom);
		st->new_stack_bottom += f->size;

		switch (f->resume(f, context)) {
		case EU_PARSE_OK:
			break;

		case EU_PARSE_REINSTATE_PAUSED:
			st->new_stack_bottom -= f->size;
			/* fall through */

		case EU_PARSE_PAUSED:
			goto out_ok;

		case EU_PARSE_ERROR:
			goto out_error;
		}
	}

	/* Process frames from the old stack */
	while (st->old_stack_bottom != st->stack_area_size) {
		f = (struct eu_stack_frame *)(st->stack + st->old_stack_bottom);
		st->old_stack_bottom += f->size;

		switch (f->resume(f, context)) {
		case EU_PARSE_OK:
			break;

		case EU_PARSE_REINSTATE_PAUSED:
			st->old_stack_bottom -= f->size;
			/* fall through */

		case EU_PARSE_PAUSED:
			goto out_ok;

		case EU_PARSE_ERROR:
			goto out_error;
		}
	}

 out_ok:
	return 1;

 out_error:
	return 0;
}

int eu_stack_reserve_scratch(struct eu_stack *st, size_t s)
{
	size_t new_stack_size, old_stack_size, min_size;
	char *stack;

	if (st->new_stack_bottom != st->new_stack_top) {
		/* There is a new stack region */
		if (s <= st->new_stack_bottom)
			return 1;

		/* Can we make space by consolidating the new stack
		   with the old_stack? */
		new_stack_size = st->new_stack_top - st->new_stack_bottom;
		if (s <= st->old_stack_bottom - new_stack_size) {
			eu_stack_begin_pause(st);
			return 1;
		}
	}
	else {
		if (s <= st->old_stack_bottom)
			return 1;

		new_stack_size = 0;
	}

	/* Need to grow the stack area */
	old_stack_size = st->stack_area_size - st->old_stack_bottom;
	min_size = ROUND_UP(s) + new_stack_size + old_stack_size;

	do {
		st->stack_area_size *= 2;
	} while (st->stack_area_size < min_size);

	stack = malloc(st->stack_area_size);
	if (stack == NULL)
		return 0;

	memcpy(stack, st->stack, st->scratch_size);

	memcpy(stack + st->stack_area_size - old_stack_size,
	       st->stack + st->old_stack_bottom,
	       old_stack_size);
	st->old_stack_bottom = st->stack_area_size - old_stack_size;

	memcpy(stack + st->old_stack_bottom - new_stack_size,
	       st->stack + st->new_stack_bottom, new_stack_size);
	st->new_stack_top = st->old_stack_bottom;
	st->new_stack_bottom = st->new_stack_top - new_stack_size;

	free(st->stack);
	st->stack = stack;
	return 1;
}

int eu_stack_set_scratch(struct eu_stack *st, const char *start, const char *end)
{
	size_t len = end - start;

	if (eu_stack_reserve_scratch(st, len)) {
		memcpy(st->stack, start, len);
		st->scratch_size = len;
		return 1;
	}
	else {
		return 0;
	}
}

int eu_stack_append_scratch(struct eu_stack *st, const char *start,
			    const char *end)
{
	size_t len = end - start;

	if (eu_stack_reserve_scratch(st, st->scratch_size + len)) {
		memcpy(st->stack + st->scratch_size, start, len);
		st->scratch_size += len;
		return 1;
	}
	else {
		return 0;
	}
}

int eu_stack_append_scratch_with_nul(struct eu_stack *st, const char *start,
				     const char *end)
{
	size_t len = end - start;

	if (eu_stack_reserve_scratch(st, st->scratch_size + len + 1)) {
		memcpy(st->stack + st->scratch_size, start, len);
		st->scratch_size += len;
		st->stack[st->scratch_size++] = 0;
		return 1;
	}
	else {
		return 0;
	}
}

void eu_stack_fini(struct eu_stack *st)
{
	struct eu_stack_frame *f;

	/* If the parse was unfinished, there might be stack frames to
	   clean up. */
	while (st->new_stack_bottom != st->new_stack_top) {
		f = (struct eu_stack_frame *)(st->stack + st->new_stack_bottom);
		st->new_stack_bottom += f->size;
		f->destroy(f);
	}

	while (st->old_stack_bottom != st->stack_area_size) {
		f = (struct eu_stack_frame *)(st->stack + st->old_stack_bottom);
		st->old_stack_bottom += f->size;
		f->destroy(f);
	}

	free(st->stack);
}

void eu_stack_frame_noop_destroy(struct eu_stack_frame *cont)
{
	(void)cont;
}
