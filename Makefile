CFLAGS=-Wall -Wextra -ansi -g

.PHONY:
all:: euphemus

euphemus: euphemus.c
	$(CC) $(CFLAGS) euphemus.c -o euphemus

.PHONY: clean
clean::
	rm -f euphemus
