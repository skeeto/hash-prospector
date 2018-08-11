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

Article: [Prospecting for Hash Functions][article]

## Discovered Hash Functions

This 32-bit integer hash function has the lowest bias of any in this
form ever devised. It even beats the venerable MurmurHash3 32-bit
finalizer by a tiny margin. The hash function construction was
discovered by the prospector, then the parameters were tuned using a
genetic algorithm.

```c
// exact bias: 0.19768193144773874
uint32_t
lowbias32(uint32_t x)
{
    x ^= x >> 18;
    x *= UINT32_C(0xa136aaad);
    x ^= x >> 16;
    x *= UINT32_C(0x9f6d62d7);
    x ^= x >> 17;
    return x;
}
```

This next function was discovered using only the prospector. It has a bit more
bias than the previous function.

```c
// exact bias: 0.34968228323361017
uint32_t
prospector32(uint32_t x)
{
    x ^= x >> 15;
    x *= UINT32_C(0x2c1b3c6d);
    x ^= x >> 12;
    x *= UINT32_C(0x297a2d39);
    x ^= x >> 15;
    return x;
}
```

To use the prospector search randomly for alternative multiplication constants,
run it like so:

    $ ./prospector -p xorr:15,mul,xorr:12,mul,xorr:15

Another round of multiply-xorshift in this construction allows functions
with carefully chosen parameters to approach the theoretical bias limit
(bias = ~0.0217). For example, this hash function is *nearly*
indistinguishable from a perfect PRF:

```c
// exact bias: 0.022829781930394154
uint32_t
triple32(uint32_t x)
{
    x ^= x >> 18;
    x *= UINT32_C(0xed5ad4bb);
    x ^= x >> 12;
    x *= UINT32_C(0xac4c1b51);
    x ^= x >> 17;
    x *= UINT32_C(0xc0a8e5d7);
    x ^= x >> 12;
    return x;
}
```

## Measuring exact bias

The `-E` mode evaluates the bias of a given hash function (`-p` or `-l`). By
default the prospector uses an estimate to quickly evaluate a function's bias,
but it's non-deterministic and there's a lot of noise in the result. To
exhaustively measure the exact bias, use the `-e` option.

The function to be checked can be defined using `-p` and a pattern or
`-l` and a shared library containing a function named `hash()`. For
example, to measure the exact bias of the best hash function above:

    $ ./prospector -Eep xorr:16,mul:e2d0d4cb,xorr:15,mul:3c6ad939,xorr:15

Or drop the function in a C file named hash.c, and name the function
`hash()`. This lets you test hash functions that can't be represented
using the prospector's limited notion of hash functions.

    $ cc -O3 -shared -fPIC -l hash.so hash.c
    $ ./prospector -Eel ./hash.so

By default it treats its input as a 32-bit hash function. Use the `-8`
switch to test (by estimation) 64-bit functions. There is no exact,
exhaustive test for 64-bit hash functions since that would take far too
long.

## Reversible operation selection

```c
x  = ~x;
x ^= constant;
x *= constant | 1; // e.g. only odd constants
x += constant;
x ^= x >> constant;
x ^= x << constant;
x += x << constant;
x -= x << constant;
x <<<= constant; // left rotation
```

Technically `x = ~x` is covered by `x = ^= constant`. However, `~x` is
uniquely special and particularly useful. The generator is very unlikely
to generate the one correct constant for the XOR operator that achieves
the same effect.


[article]: https://nullprogram.com/blog/2018/07/31/
[jenkins]: http://burtleburtle.net/bob/hash/integer.html
[rev]: http://papa.bretmulvey.com/post/124027987928/hash-functions
[wang]: https://gist.github.com/badboy/6267743
