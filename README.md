# Hash Function Prospector

This is a little tool for automated [integer hash function][wang]
discovery. It generates billions of [integer hash functions][jenkins] at
random from a selection of [nine reversible operations][rev]. The
generated functions are JIT compiled and their avalanche behavior is
evaluated. The current best function is printed out in C syntax.

The *avalanche score* is the number of output bits that remain "fixed"
on average when a single input bit is flipped. Lower scores are better.
Ideally the score is 0 — e.g. every output bit flips with a 50% chance
when a single input bit is flipped.

Prospector can generate both 32-bit and 64-bit integer hash functions.
Check the usage (`-h`) for the full selection of options. Due to the JIT
compiler, only x86-64 is supported, though the functions it discovers
can, of course, be used anywhere.

## Discovered Functions

So far I've used prospector to discover these two high quality 32-bit
integer hash functions:

```c
/* Avalanche score = 1.83
 * Compiles to only 23 bytes on x86-64
 * High avalanche
 * 6 billion hashes / second (Haswell)
 */
uint32_t
mosquito32(uint32_t x)
{
    x  = ~x;
    x ^= x >> 16;
    x *= UINT32_C(0xb03a22b3);
    x ^= x >> 10;
    return x;
}

/* Avalanche score = 1.51
 * Very effective avalanche
 * 3.3 billion hashes / second (Haswell)
 */
uint32_t
skeeto32(uint32_t x)
{
    x  = ~x;
    x ^= x >> 2;
    x += x << 21;
    x ^= x >> 15;
    x ^= x << 5;
    x ^= x >> 9;
    x ^= x << 13;
    return x;
}
```

## Reversible operation selection

```c
x  = ~x;
x ^= constant;
x *= constant; // only odd constants
x += constant;
x ^= x >> constant;
x ^= x << constant;
x += x << constant;
x -= x << constant;
x = (x << constant) | (x >> (nbits - constant));
```

Technically `x = ~x` is covered by `x = ^= constant`. However, `~x` is
uniquely special and particularly useful. The generator is very unlikely
to generate the one correct constant for the XOR operator that achieves
the same effect.


[rev]: http://papa.bretmulvey.com/post/124027987928/hash-functions
[wang]: https://gist.github.com/badboy/6267743
[jenkins]: http://burtleburtle.net/bob/hash/integer.html
