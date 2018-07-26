CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -march=native -O3 -ggdb3

prospector: prospector.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ prospector.c $(LDLIBS)

clean:
	rm -f prospector
