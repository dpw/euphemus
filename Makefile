CFLAGS=-Wall -Wextra -ansi -g

.PHONY:
all:: test

test: test.c parse.c struct.c string.c variant.c
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean
clean::
	rm -f test
