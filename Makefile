CFLAGS=-Wall -Wextra -ansi -g

.PHONY:
all:: test test_parse

test: test.c parse.c struct.c string.c variant.c number.c struct_sm.c
	$(CC) $(CFLAGS) test.c parse.c struct.c string.c variant.c number.c -o $@

test_parse: test_parse.c parse.c struct.c string.c variant.c number.c struct_sm.c
	$(CC) $(CFLAGS) test_parse.c parse.c struct.c string.c variant.c number.c -o $@

.PHONY: clean
clean::
	rm -f test test_parse
