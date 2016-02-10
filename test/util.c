#include <stdio.h>
#include <stdlib.h>

char *read_file(const char *fname, size_t *len_out)
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

