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

This hash function has an extremely low bias. The only 32-bit function
I've seen with an even lower bias is the MurmurHash3 finalizer.

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

To search for alternative multiplication constants, run the prospector
like so:

    $ ./prospector -p xorr:15,mul,xorr:12,mul,xorr:15

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
