CC := gcc
CFLAGS := -std=gnu11 -Wall -Wextra -Wpedantic -O2

all: witsshell

witsshell: witsshell.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f witsshell

.PHONY: all clean
