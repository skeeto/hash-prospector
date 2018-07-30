CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -march=native -O3 -ggdb3
LDFLAGS = -lm -ldl

prospector: prospector.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ prospector.c $(LDLIBS)

tests/degski64.so: tests/degski64.c
tests/h2hash32.so: tests/h2hash32.c
tests/hash32shift.so: tests/hash32shift.c
tests/splitmix64.so: tests/splitmix64.c

hashes = \
    tests/degski64.so \
    tests/h2hash32.so \
    tests/hash32shift.so \
    tests/splitmix64.so

check: prospector $(hashes)
	./prospector -E -8 -l tests/degski64.so
	./prospector -E -4 -l tests/h2hash32.so
	./prospector -E -4 -l tests/hash32shift.so
	./prospector -E -8 -l tests/splitmix64.so

clean:
	rm -f prospector $(hashes)

.SUFFIXES: .so .c
.c.so:
	$(CC) -shared $(LDFLAGS) -fPIC $(CFLAGS) -o $@ $<
