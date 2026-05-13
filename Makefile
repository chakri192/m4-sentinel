CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Os -framework CoreFoundation -framework IOKit

sentinel: sentinel.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f sentinel
