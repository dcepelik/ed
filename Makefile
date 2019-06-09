.PHONY: clean test

PROG := ed

CFLAGS += -std=c99 -Wall -Werror -Wextra -Wpedantic -g

$(PROG): $(PROG).c Makefile
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f -- $(PROG) vgcore.*

test: $(PROG)
	cd tests; ./run
