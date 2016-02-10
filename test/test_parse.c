#include <stdio.h>
#include <stdlib.h>

#include <euphemus.h>

#include "util.h"

int main(int argc, char **argv)
{
	char *json;
	size_t json_len, i;
	struct eu_parse *parse;
	struct eu_variant var;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [ filename ]\n", argv[0]);
		exit(1);
	}

	json = read_file(argc == 2 ? argv[1] : NULL, &json_len);

	/* Test parsing in one go */
	parse = eu_parse_create(eu_variant_value(&var));
	if (!eu_parse(parse, json, json_len))
		goto error;

	if (!eu_parse_finish(parse))
		goto error;

	eu_parse_destroy(parse);
	eu_variant_fini(&var);

	/* Test parsing char by char */
	parse = eu_parse_create(eu_variant_value(&var));
	for (i = 0; i < json_len; i++) {
		char c = json[i];
		if (!eu_parse(parse, &c, 1))
			goto error;
	}

	if (!eu_parse_finish(parse))
		goto error;

	eu_parse_destroy(parse);
	eu_variant_fini(&var);

	free(json);
	return 0;

 error:
	fprintf(stderr, "parse error\n");
	return 1;
}
