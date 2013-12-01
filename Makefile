CFLAGS=-Wall -Wextra -ansi -g

.PHONY:
all:: test test_parse

test: test.c parse.c struct.c string.c variant.c
	$(CC) $(CFLAGS) $^ -o $@

test_parse: test_parse.c parse.c struct.c string.c variant.c
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean
clean::
	rm -f test test_parse
