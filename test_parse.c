#include <stdio.h>

#include "euphemus.h"

int main(void)
{
	int status = 0;
	struct eu_parse parse;
	struct eu_variant var;

	eu_parse_init_variant(&parse, &var);
	while (!feof(stdin)) {
		char buf[1000];
		size_t res = fread(buf, 1, 1000, stdin);

		if (res < 1000 && ferror(stdin)) {
			perror("reading");
			status = 1;
			goto out;
		}

		if (!eu_parse(&parse, buf, res)) {
			fprintf(stderr, "parse error\n");
			status = 1;
			goto out;
		}
	}

	if (!eu_parse_finish(&parse)) {
		fprintf(stderr, "parse error\n");
		status = 1;
	}

 out:
	eu_parse_fini(&parse);
	return status;
}
