#include <euphemus.h>
#include "euphemus_int.h"

struct eu_generate *eu_generate_create(struct eu_value value)
{
	struct eu_generate *eg = malloc(sizeof *eg);
	eg->value = value;
	eg->error = 0;
	return eg;
}

size_t eu_generate(struct eu_generate *eg, char *output, size_t len)
{
	const struct eu_metadata *md = eg->value.metadata;

	if (unlikely(eg->error))
		return 0;

	eg->output = output;
	eg->output_end = output + len;

	if (md->generate(md, eg, eg->value.value) == EU_GEN_OK) {
		return eg->output - output;
	}
	else {
		eg->error = 1;
		return eg->output - output;
	}
}

int eu_generate_ok(struct eu_generate *eg)
{
	return !eg->error;
}

void eu_generate_destroy(struct eu_generate *eg)
{
	free(eg);
}
