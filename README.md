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

There are two useful classes of hash functions discovered by the
prospector and the other helper utilities here. Both use an
*xorshift-multiply-xorshift* construction, but with a different number
of rounds.

### Two round functions

This 32-bit integer hash function has the lowest bias of any in this
form ever devised. It even beats the venerable MurmurHash3 32-bit
finalizer by a tiny margin. The hash function construction was
discovered by the prospector, then the parameters were tuned using hill
climbing and a genetic algorithm.

```c
// exact bias: 0.17353355999581582
uint32_t
lowbias32(uint32_t x)
{
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
    x *= UINT32_C(0x846ca68b);
    x ^= x >> 16;
    return x;
}

// inverse
uint32_t
lowbias32_r(uint32_t x)
{
    x ^= x >> 16;
    x *= UINT32_C(0x43021123);
    x ^= x >> 15 ^ x >> 30;
    x *= UINT32_C(0x1d69e2a5);
    x ^= x >> 16;
    return x;
}
```

Here are some alternate constants with low bias:

    [16 a812d533 15 b278e4ad 17] = 0.16540778981744320
    [15 f15f5959 14 7db29359 16] = 0.18103205436627479
    [17 7186cd35 15 fe6bba73 15] = 0.18312741727971640
    [16 93f2552b 15 959b4a4d 15] = 0.18360629205797341
    [16 df892d4b 15 3c2da6b3 16] = 0.18368195486921446
    [15 4811acab 15 5591acd7 16] = 0.18522661033580071
    [16 dc85aaa7 15 6658a5cb 15] = 0.18577280285788791
    [16 1ec9b4db 15 3224d38d 17] = 0.18631684392389897
    [16 462daaad 15 0a36c95d 16] = 0.18674876992866513
    [16 17cdd657 15 a426cb25 15] = 0.18995262675473334
    [17 cd8512ad 15 b95c5a73 15] = 0.19050717016846502
    [16 aecc96b5 15 f64dcd47 15] = 0.19077817816874504
    [15 2548acd5 15 0b39d397 16] = 0.19121161052714156
    [15 1216ccb5 15 3abcdca9 15] = 0.19426091938816648
    [16 c845a997 15 f214db9b 17] = 0.19553179377831409
    [17 179cd515 15 4c495d47 15] = 0.19608530402798924
    [16 b921a6cb 14 30b5a6d1 16] = 0.19745192295417058
    [18 a136aaad 16 9f6d62d7 17] = 0.19768193144773874
    [17 24f4d2cd 15 1ba3b969 16] = 0.19789489706453650
    [17 9bde596b 16 1c9e9647 16] = 0.19882570872036193
    [16 08496335 14 578a7297 16] = 0.20075510820626494
    [15 08d55695 15 5f2e5ac7 15] = 0.20168904295517728
    [16 e2d0d4cb 15 3c6ad939 15] = 0.20207553121367283
    [16 58dccb4b 13 f3dc6ca9 16] = 0.20215701510198184
    [16 3da06567 14 d5d692e5 16] = 0.20461270421499261
    [17 5abe3ae5 13 65639657 16] = 0.20650238245274932
    [16 849e54d3 15 4e2449d3 15] = 0.20719603210649459
    [16 c4d82b4b 15 4959b58b 16] = 0.20750552416032758
    [15 18b2b2cd 15 5175cad9 15] = 0.20828822711613942
    [17 6e79e54b 14 0915b24d 16] = 0.20910767680641257

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

### Three round functions

Another round of multiply-xorshift in this construction allows functions
with carefully chosen parameters to reach the theoretical bias limit
(bias = ~0.021). For example, this hash function is indistinguishable
from a perfect PRF (e.g. a random permutation of all 32-bit integers):

```c
// exact bias: 0.020888578919738908
uint32_t
triple32(uint32_t x)
{
    x ^= x >> 17;
    x *= UINT32_C(0xed5ad4bb);
    x ^= x >> 11;
    x *= UINT32_C(0xac4c1b51);
    x ^= x >> 15;
    x *= UINT32_C(0x31848bab);
    x ^= x >> 14;
    return x;
}

// inverse
uint32_t
triple32_r(uint32_t x)
{
    x ^= x >> 14 ^ x >> 28;
    x *= UINT32_C(0x32b21703);
    x ^= x >> 15 ^ x >> 30;
    x *= UINT32_C(0x469e0db1);
    x ^= x >> 11 ^ x >> 22;
    x *= UINT32_C(0x79a85073);
    x ^= x >> 17;
    return x;
}
```

And here are some alternate constants which are nearly as unbiased:

    [16 aeccedab 14 ac613e37 16 19c89935 17] = 0.021246568167078764
    [16 236f7153 12 33cd8663 15 3e06b66b 16] = 0.021280991798512679
    [18 4260bb47 13 27e8e1ed 15 9d48a33b 15] = 0.021576730651802156
    [17 3f6cde45 12 51d608ef 16 6e93639d 17] = 0.021772288363808408
    [15 5dfa224b 14 4bee7e4b 17 930ee371 15] = 0.02184521628884813
    [17 3964f363 14 9ac3751d 16 4e8772cb 17] = 0.021883292578109576
    [16 66046c65 14 d3f0865b 16 f9999193 16] = 0.0219446068365007
    [16 b1a89b33 14 09136aaf 16 5f2a44a7 15] = 0.021998624107282542
    [16 24767aad 12 daa18229 16 e9e53beb 16] = 0.022043911220395354
    [15 42f91d8d 14 61355a85 15 dcf2a949 14] = 0.022052539152635078
    [15 4df8395b 15 466b428b 16 b4b2868b 16] = 0.022140187420461286
    [16 2bbed51b 14 cd09896b 16 38d4c587 15] = 0.022159936298777144
    [16 0ab694cd 14 4c139e47 16 11a42c3b 16] = 0.02220928191220355
    [17 7f1e072b 12 8750a507 16 ecbb5b5f 16] = 0.022283743052847804
    [16 f1be7bad 14 73a54099 15 3b85b963 15] = 0.022316544125749647
    [16 66e756d5 14 b5f5a9cd 16 84e56b11 16] = 0.022372957847491555
    [15 233354bb 15 ce1247bd 16 855089bb 17] = 0.022406591070966285
    [16 eb6805ab 15 d2c7b7a7 16 7645a32b 16] = 0.022427060650927547
    [16 8288ab57 14 0d1bfe57 16 131631e5 16] = 0.022431656871313443
    [16 45109e55 14 3b94759d 16 adf31ea5 17] = 0.022436433678417977
    [15 26cd1933 14 e3da1d59 16 5a17445d 16] = 0.022460520416491526
    [16 7001e6eb 14 bb8e7313 16 3aa8c523 15] = 0.022491767264054854
    [16 49ed0a13 14 83588f29 15 658f258d 15] = 0.022500668856510898
    [16 6cdb9705 14 4d58d2ed 14 c8642b37 16] = 0.022504626537729222
    [16 a986846b 14 bdd5372d 15 ad44de6b 17] = 0.022528238323120016
    [16 c9575725 15 9448f4c5 16 3b7a5443 16] = 0.022586511310042686
    [15 fc54c453 13 08213789 15 669f96eb 16] = 0.022591114646032095
    [16 d47ef17b 14 642fa58f 16 a8b65b9b 16] = 0.022600633971701509
    [15 00bfaa73 14 8799c69b 16 731985b1 16] = 0.022645866629596379
    [16 953a55e9 15 8523822b 17 56e7aa63 15] = 0.022667180032713324
    [16 a3d7345b 15 7f41c9c7 16 308bd62d 17] = 0.022688845770122031
    [16 195565c7 14 16064d6f 16 0f9ec575 15] = 0.022697810688752193
    [16 13566dbb 14 59369a03 15 990f9d1b 16] = 0.022712430070797596
    [16 8430cc4b 15 a7831cbd 15 c6ccbd33 15] = 0.022734765033419774
    [16 699f272b 14 09c01023 16 39bd48c3 15] = 0.022854175321846512
    [15 336536c3 13 4f0e38b1 16 15d229f7 16] = 0.022884125170795171
    [16 221f686d 12 d8948a07 16 ed8a8345 16] = 0.022902500408830236
    [16 d7ca8cbb 13 eb4e259f 15 34ab1143 16] = 0.022905955538176669
    [16 7cb04f65 14 9b96da73 16 83625687 15] = 0.022906573700088178
    [15 5156196b 14 940d8869 15 0086f473 17] = 0.022984943828687553

Prepending an increment to `triple32` breaks the `hash(0) = 0` issue while
also lowering the bias a tiny bit further:

```c
// exact bias: 0.020829410544597495
uint32_t
triple32inc(uint32_t x)
{
    x++;
    x ^= x >> 17;
    x *= UINT32_C(0xed5ad4bb);
    x ^= x >> 11;
    x *= UINT32_C(0xac4c1b51);
    x ^= x >> 15;
    x *= UINT32_C(0x31848bab);
    x ^= x >> 14;
    return x;
}

// inverse
uint32_t
triple32inc_r(uint32_t x)
{
    x ^= x >> 14 ^ x >> 28;
    x *= UINT32_C(0x32b21703);
    x ^= x >> 15 ^ x >> 30;
    x *= UINT32_C(0x469e0db1);
    x ^= x >> 11 ^ x >> 22;
    x *= UINT32_C(0x79a85073);
    x ^= x >> 17;
    x--;
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
