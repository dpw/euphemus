#include <euphemus.h>
#include "euphemus_int.h"

struct eu_generate *eu_generate_create(struct eu_value value)
{
	struct eu_generate *eg = malloc(sizeof *eg);
	eg->value = value;
	eg->error = 0;
	return eg;
}

void eu_generate_destroy(struct eu_generate *eg)
{
	free(eg);
}
