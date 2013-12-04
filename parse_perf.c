#include <stdio.h>
#include <stdlib.h>

#include <json/json.h>
#include "euphemus.h"


static char *read_file(const char *fname, size_t *len_out)
{
	FILE *fp;
	char *buf;
	size_t capacity = 1000;
	size_t len = 0;

	if (fname) {
		fp = fopen(fname, "r");
		if (!fp) {
			perror("opening");
			exit(1);
		}
	}
	else {
		fp = stdin;
	}

	buf = malloc(capacity);
	if (!buf) {
		fprintf(stderr, "malloc failed\n");
		exit(1);
	}

	for (;;) {
		len += fread(buf + len, 1, capacity - len, fp);
		if (feof(fp))
			break;

		if (ferror(fp)) {
			perror("reading");
			exit(1);
		}

		if (len == capacity) {
			capacity *= 2;
			buf = realloc(buf, capacity);
			if (!buf) {
				fprintf(stderr, "realloc failed\n");
				exit(1);
			}
		}
	}

	if (fname)
		fclose(fp);

	*len_out = len;
	return buf;
}

int main(int argc, char **argv)
{
	char *json;
	size_t json_len;
	int i;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [ filename ]\n", argv[0]);
		exit(1);
	}

	json = read_file(argc == 2 ? argv[1] : NULL, &json_len);

#if 1
	for (i = 0; i < 10000; i++) {
		struct eu_parse parse;
		struct eu_variant var;

		eu_parse_init_variant(&parse, &var);
		if (!eu_parse(&parse, json, json_len))
			goto error;

		if (!eu_parse_finish(&parse))
			goto error;

		eu_parse_fini(&parse);
		eu_variant_fini(&var);
	}
#else
	for (i = 0; i < 10000; i++) {
		json_object *jobj;
		struct json_tokener *tok = json_tokener_new();
		jobj = json_tokener_parse_ex(tok, json, json_len);
		if (json_tokener_get_error(tok) != json_tokener_success)
			goto error;
		json_tokener_free(tok);
		json_object_put(jobj);
	}
#endif

	free(json);
	return 0;

 error:
	fprintf(stderr, "parse error\n");
	return 1;
}
