#define _DEFAULT_SOURCE // MAP_ANONYMOUS
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <fcntl.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>

#if defined (__SSSE3__) || defined (__PCLMUL__)
#include <immintrin.h>
 #ifdef __SSSE3__
 #define HAVE_SHF        // we have SSSE3's byte SHuFfle
 #endif
 #ifdef __PCLMUL__
 #define HAVE_CLMUL      // we have CarryLess MULtiplication
 #endif
#endif




#define ABI __attribute__((sysv_abi))

#define countof(a) ((int)(sizeof(a) / sizeof(0[a])))

static uint64_t
xoroshiro128plus(uint64_t s[2])
{
    uint64_t s0 = s[0];
    uint64_t s1 = s[1];
    uint64_t result = s0 + s1;
    s1 ^= s0;
    s[0] = ((s0 << 24) | (s0 >> 40)) ^ s1 ^ (s1 << 16);
    s[1] = (s1 << 37) | (s1 >> 27);
    return result;
}

enum hf_type {
    /* 32 bits */
    HF32_XOR,  // x ^= const32
#ifdef HAVE_CLMUL
    HF32_CLMUL,// x  = _mm_clmulepi64_si128(x, const32, opSelect)
#endif
    HF32_MUL,  // x *= const32 (odd)
    HF32_ADD,  // x += const32
    HF32_ROT,  // x  = (x << const5) | (x >> (32 - const5)) a.k.a. (x <<< const5)
    HF32_NOT,  // x  = ~x
    HF32_BSWAP,// x  = bswap32(x)
#ifdef HAVE_SHF
    HF32_SHF,  // x  = _mm_shuffle_epi8(x, const32)
#endif
    HF32_XORL, // x ^= x << const5
    HF32_XORR, // x ^= x >> const5
    HF32_ADDL, // x += x << const5
    HF32_SUBL, // x -= x << const5
    HF32_XROT2,// x ^= (x <<< aConst5) ^ (x <<< bConst5)
    /* 64 bits */
    HF64_XOR,
#ifdef HAVE_CLMUL
    HF64_CLMUL,
#endif
    HF64_MUL,
    HF64_ADD,
    HF64_ROT,
    HF64_NOT,
    HF64_BSWAP,
#ifdef HAVE_SHF
    HF64_SHF,
#endif
    HF64_XORL,
    HF64_XORR,
    HF64_ADDL,
    HF64_SUBL,
    HF64_XROT2
};

static const char hf_names[][8] = {
    [HF32_XOR]  = "32xor",
#ifdef HAVE_CLMUL
    [HF32_CLMUL]= "32clmul",
#endif
    [HF32_MUL]  = "32mul",
    [HF32_ADD]  = "32add",
    [HF32_ROT]  = "32rot",
    [HF32_NOT]  = "32not",
    [HF32_BSWAP]= "32bswap",
#ifdef HAVE_SHF
    [HF32_SHF]  = "32shf",
#endif
    [HF32_XORL] = "32xorl",
    [HF32_XORR] = "32xorr",
    [HF32_ADDL] = "32addl",
    [HF32_SUBL] = "32subl",
    [HF32_XROT2]= "32xrot2",
    [HF64_XOR]  = "64xor",
#ifdef HAVE_CLMUL
    [HF64_CLMUL]= "64clmul",
#endif
    [HF64_MUL]  = "64mul",
    [HF64_ADD]  = "64add",
    [HF64_ROT]  = "64rot",
    [HF64_NOT]  = "64not",
    [HF64_BSWAP]= "64bswap",
#ifdef HAVE_SHF
    [HF64_SHF]  = "64shf",
#endif
    [HF64_XORL] = "64xorl",
    [HF64_XORR] = "64xorr",
    [HF64_ADDL] = "64addl",
    [HF64_SUBL] = "64subl",
    [HF64_XROT2]= "64xrot2"
};

#define FOP_LOCKED  (1 << 0)
struct hf_op {
    enum hf_type type;
    uint64_t constant0;
    uint64_t constant1;
    int flags;
};

#define rol(n,x,r) (((x) << r) | ((x) >> (n - r)))
#define ror(n,x,r) (((x) >> r) | ((x) << (n - r)))

/* Randomize the constants of the given hash operation.
 */
static void
hf_randomize(struct hf_op *op, uint64_t s[2])
{
    uint64_t r = xoroshiro128plus(s);
    switch (op->type) {
        case HF32_NOT:
        case HF64_NOT:
        case HF32_BSWAP:
        case HF64_BSWAP:
            op->constant0 = 0;
            break;
#ifdef HAVE_SHF
        case HF32_SHF: {
            // 'inside-out' version of Fishes-Yates shuffle;
            // taken from 'https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle'
            // and considered in the public domain;
            // using byte positions in uint32_t or uint64_t, respectively
            uint32_t c = 0;
            for(int i = 0; i < (int)sizeof(c); i++) {
                c <<= 8;
                r = (uint32_t)xoroshiro128plus(s) % (i + 1);
                if(r) c |= (uint8_t)(c >> (r * 8));                                            // a[i] = a[r]
                c = ror(32, c, (r * 8)); c &= ~(uint32_t)0xff; c |= i; c = rol(32, c, r * 8);  // a[r] = i
            }
            op->constant0 = c;
            break;
        }
        case HF64_SHF: {
            uint64_t c = 0;
            for(int i = 0; i < (int)sizeof(c); i++) {
                c <<= 8;
                r = (uint32_t)xoroshiro128plus(s) % (i + 1);
                if(r) c |= (uint8_t)(c >> (r * 8));                                     // a[i] = a[r]
                c = ror(64, c, (r * 8)); c &= ~0xffull; c |= i; c = rol(64, c, r * 8);  // a[r] = i
            }
            op->constant0 = c;
            break;
        }
#endif
        case HF32_XOR:
        case HF32_ADD:
            op->constant0 = (uint32_t)r;
            break;
#ifdef HAVE_CLMUL
        case HF32_CLMUL:
#endif
        case HF32_MUL:
            op->constant0 = (uint32_t)r | 1;
            break;
        case HF32_ROT:
        case HF32_XORL:
        case HF32_XORR:
        case HF32_ADDL:
        case HF32_SUBL:
            op->constant0 = 1 + r % 31;
            break;
        case HF32_XROT2:
            op->constant0 = 1 + r % 31;
            op->constant1 = 1 + (r >> 10) % 31;
            if (op->constant1 == op->constant0) {
                // do not allow both constants to be equal
                op->constant 1 + ((r >> 10) + 1) % 31;
            }
            break;
        case HF64_XOR:
        case HF64_ADD:
            op->constant0 = r;
            break;
#ifdef HAVE_CLMUL
        case HF64_CLMUL:
#endif
        case HF64_MUL:
            op->constant0 = r | 1;
            break;
        case HF64_ROT:
        case HF64_XORL:
        case HF64_XORR:
        case HF64_ADDL:
        case HF64_SUBL:
            op->constant0 = 1 + r % 63;
            break;
        case HF64_XROT2:
            op->constant0 = 1 + r % 63;
            op->constant1 = 1 + (r >> 12) % 63;
            break;
    }
}

#define F_U64     (1 << 0)
#define F_TINY    (1 << 1)  // don't use big constants

static void
hf_gen(struct hf_op *op, uint64_t s[2], int flags)
{
    uint64_t r = xoroshiro128plus(s);
    int min = flags & F_TINY ? 3 : 0;
    op->type = (r % (9 - min)) + min + (flags & F_U64 ? 9 : 0);
    hf_randomize(op, s);
}

/* Return 1 if these operations may be adjacent
*/
static int
hf_type_valid(enum hf_type a, enum hf_type b)
{
    switch (a) {
        case HF32_NOT:
        case HF32_BSWAP:
#ifdef HAVE_SHF
        case HF32_SHF:
#endif
        case HF32_XOR:
#ifdef HAVE_CLMUL
        case HF32_CLMUL:
#endif
        case HF32_MUL:
        case HF32_ADD:
        case HF32_ROT:
        case HF64_NOT:
        case HF64_BSWAP:
#ifdef HAVE_SHF
        case HF64_SHF:
#endif
        case HF64_XOR:
#ifdef HAVE_CLMUL
        case HF64_CLMUL:
#endif
        case HF64_MUL:
        case HF64_ADD:
        case HF64_ROT:
            return a != b;
        case HF32_XORL:
        case HF32_XORR:
        case HF32_ADDL:
        case HF32_SUBL:
        case HF32_XROT2:
        case HF64_XORL:
        case HF64_XORR:
        case HF64_ADDL:
        case HF64_SUBL:
        case HF64_XROT2:
            return 1;
    }
    abort();
}

static void
hf_genfunc(struct hf_op *ops, int n, int flags, uint64_t s[2])
{
    hf_gen(ops, s, flags);
    for (int i = 1; i < n; i++) {
        do {
            hf_gen(ops + i, s, flags);
        } while (!hf_type_valid(ops[i - 1].type, ops[i].type));
    }
}

/* Randomize the parameters of the given function.
 */
static void
hf_randfunc(struct hf_op *ops, int n, uint64_t s[2])
{
    for (int i = 0; i < n; i++)
        if (!(ops[i].flags & FOP_LOCKED))
            hf_randomize(ops + i, s);
}

static void
hf_print(const struct hf_op *op, char *buf)
{
    unsigned long long c = op->constant0;
    unsigned long long d = op->constant1;
    switch (op->type) {
        case HF32_NOT:
        case HF64_NOT:
            sprintf(buf, "x  = ~x;");
            break;
        case HF32_BSWAP:
            sprintf(buf, "x  = __builtin_bswap32(x);");
            break;
        case HF64_BSWAP:
            sprintf(buf, "x  = __builtin_bswap64(x);");
            break;
#ifdef HAVE_SHF
        case HF32_SHF:
            sprintf(buf, "x = _mm_cvtsi128_si32(_mm_shuffle_epi8(_mm_cvtsi32_si128(x), _mm_cvtsi32_si128(0x%08llx));", c);
            break;
#endif
        case HF32_XOR:
            sprintf(buf, "x ^= 0x%08llx;", c);
            break;
#ifdef HAVE_CLMUL
        case HF32_CLMUL:
            sprintf(buf, "x = _mm_cvtsi128_si32(_mm_clmulepi64_si128(_mm_cvtsi32_si128(x), _mm_cvtsi32_si128(0x%08llx), 0x00));", c);
            break;
#endif
        case HF32_MUL:
            sprintf(buf, "x *= 0x%08llx;", c);
            break;
        case HF32_ADD:
            sprintf(buf, "x += 0x%08llx;", c);
            break;
        case HF32_ROT:
            sprintf(buf, "x  = (x << %llu) | (x >> %lld);", c, 32 - c);
            break;
        case HF32_XORL:
            sprintf(buf, "x ^= x << %llu;", c);
            break;
        case HF32_XORR:
            sprintf(buf, "x ^= x >> %llu;", c);
            break;
        case HF32_ADDL:
            sprintf(buf, "x += x << %llu;", c);
            break;
        case HF32_SUBL:
            sprintf(buf, "x -= x << %llu;", c);
            break;
        case HF32_XROT2:
            sprintf(buf, "x ^= ((x << %llu) | (x >> %lld)) ^ ((x << %llu) | (x >> %lld));", c, 32 - c, d , 32 - d);
            break;
#ifdef HAVE_SHF
        case HF64_SHF:
            sprintf(buf, "x = _mm_cvtsi128_si64(_mm_shuffle_epi8(_mm_cvtsi64_si128(x), _mm_cvtsi64_si128(0x%016llx)));", c);
            break;
#endif
        case HF64_XOR:
            sprintf(buf, "x ^= 0x%016llx;", c);
            break;
#ifdef HAVE_CLMUL
        case HF64_CLMUL:
            sprintf(buf, "x = _mm_cvtsi128_si64(_mm_clmulepi64_si128(_mm_cvtsi64_si128(x), _mm_cvtsi64_si128(0x%016llx), 0x00));", c);
            break;
#endif
        case HF64_MUL:
            sprintf(buf, "x *= 0x%016llx;", c);
            break;
        case HF64_ADD:
            sprintf(buf, "x += 0x%016llx;", c);
            break;
        case HF64_ROT:
            sprintf(buf, "x  = (x << %llu) | (x >> %lld);", c, 64 - c);
            break;
        case HF64_XORL:
            sprintf(buf, "x ^= x << %llu;", c);
            break;
        case HF64_XORR:
            sprintf(buf, "x ^= x >> %llu;", c);
            break;
        case HF64_ADDL:
            sprintf(buf, "x += x << %llu;", c);
            break;
        case HF64_SUBL:
            sprintf(buf, "x -= x << %llu;", c);
            break;
        case HF64_XROT2:
            sprintf(buf, "x ^= ((x << %llu) | (x >> %lld)) ^ ((x << %llu) | (x >> %lld));", c, 64 - c, d , 64 - d);
            break;
    }
}

static void
hf_printfunc(const struct hf_op *ops, int n, FILE *f)
{
    if (ops[0].type <= HF32_SUBL)
        fprintf(f, "uint32_t\nhash(uint32_t x)\n{\n");
    else
        fprintf(f, "uint64_t\nhash(uint64_t x)\n{\n");
    for (int i = 0; i < n; i++) {
        char buf[120];
        hf_print(ops + i, buf);
        fprintf(f, "    %s\n", buf);
    }
    fprintf(f, "    return x;\n}\n");
}

static unsigned char *
hf_compile(const struct hf_op *ops, int n, unsigned char *buf)
{
    if (ops[0].type <= HF32_SUBL) {
        /* mov eax, edi*/
        *buf++ = 0x89;
        *buf++ = 0xf8;
    } else {
        /* mov rax, rdi*/
        *buf++ = 0x48;
        *buf++ = 0x89;
        *buf++ = 0xf8;
    }

    for (int i = 0; i < n; i++) {
        switch (ops[i].type) {
            case HF32_NOT:
                /* not eax */
                *buf++ = 0xf7;
                *buf++ = 0xd0;
                break;
            case HF32_BSWAP:
                /* bswap eax */
                *buf++ = 0x0f;
                *buf++ = 0xc8;
                break;
#ifdef HAVE_SHF
            case HF32_SHF:
                /* mov edi, imm32 */
                *buf++ = 0xbf;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                /* movd xmm0, eax */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x6e;
                *buf++ = 0xc0;
                /* movd xmm1, edi */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x6e;
                *buf++ = 0xcf;
                /* pshufb xmm0, xmm1 */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x38;
                *buf++ = 0x00;
                *buf++ = 0xc1;
                /* movd eax, xmm0 */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x7e;
                *buf++ = 0xc0;
                break;
#endif
            case HF32_XOR:
                /* xor eax, imm32 */
                *buf++ = 0x35;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                break;
#ifdef HAVE_CLMUL
            case HF32_CLMUL:
                /* movd xmm0, eax */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x6e;
                *buf++ = 0xc0;
                /* mov edi, imm32 */
                *buf++ = 0xbf;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                /* movd xmm1, edi */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x6e;
                *buf++ = 0xcf;
                /* pclmulqdq xmm0, xmm1, 0 */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x3a;
                *buf++ = 0x44;
                *buf++ = 0xc1;
                *buf++ = 0x00;
                /* movd eax, xmm0 */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x7e;
                *buf++ = 0xc0;
                break;
#endif
            case HF32_MUL:
                /* imul eax, eax, imm32 */
                *buf++ = 0x69;
                *buf++ = 0xc0;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                break;
            case HF32_ADD:
                /* add eax, imm32 */
                *buf++ = 0x05;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                break;
            case HF32_ROT:
                /* rol eax, imm8 */
                *buf++ = 0xc1;
                *buf++ = 0xc0;
                *buf++ = ops[i].constant0;
                break;
            case HF32_XORL:
                /* mov edi, eax */
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* shl edi, imm8 */
                *buf++ = 0xc1;
                *buf++ = 0xe7;
                *buf++ = ops[i].constant0;
                /* xor eax, edi */
                *buf++ = 0x31;
                *buf++ = 0xf8;
                break;
            case HF32_XORR:
                /* mov edi, eax */
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* shr edi, imm8 */
                *buf++ = 0xc1;
                *buf++ = 0xef;
                *buf++ = ops[i].constant0;
                /* xor eax, edi */
                *buf++ = 0x31;
                *buf++ = 0xf8;
                break;
            case HF32_ADDL:
                /* mov edi, eax */
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* shl edi, imm8 */
                *buf++ = 0xc1;
                *buf++ = 0xe7;
                *buf++ = ops[i].constant0;
                /* add eax, edi */
                *buf++ = 0x01;
                *buf++ = 0xf8;
                break;
            case HF32_SUBL:
                /* mov edi, eax */
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* shl edi, imm8 */
                *buf++ = 0xc1;
                *buf++ = 0xe7;
                *buf++ = ops[i].constant0;
                /* sub eax, edi */
                *buf++ = 0x29;
                *buf++ = 0xf8;
                break;
            case HF32_XROT2:
                /* mov edi, eax */
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* rol edi, imm8 */
                *buf++ = 0xc1;
                *buf++ = 0xc7;
                *buf++ = ops[i].constant0;
                /* xor eax, edi */
                *buf++ = 0x31;
                *buf++ = 0xf8;
                /* rol edi, imm8 */
                *buf++ = 0xc1;
                *buf++ = 0xc7;
                *buf++ = (32 + ops[i].constant1 - ops[i].constant0) % 32;
                /* xor eax, edi */
                *buf++ = 0x31;
                *buf++ = 0xf8;
                break;
            case HF64_NOT:
                /* not rax */
                *buf++ = 0x48;
                *buf++ = 0xf7;
                *buf++ = 0xd0;
                break;
            case HF64_BSWAP:
                /* bswap rax */
                *buf++ = 0x48;
                *buf++ = 0x0f;
                *buf++ = 0xc8;
                break;
#ifdef HAVE_SHF
            case HF64_SHF:
                /* mov rdi, imm64 */
                *buf++ = 0x48;
                *buf++ = 0xbf;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                *buf++ = ops[i].constant0 >> 32;
                *buf++ = ops[i].constant0 >> 40;
                *buf++ = ops[i].constant0 >> 48;
                *buf++ = ops[i].constant0 >> 56;
                /* movq xmm0, rax */
                *buf++ = 0x66;
                *buf++ = 0x48;
                *buf++ = 0x0f;
                *buf++ = 0x6e;
                *buf++ = 0xc0;
                /* movq xmm1, rdi */
                *buf++ = 0x66;
                *buf++ = 0x48;
                *buf++ = 0x0f;
                *buf++ = 0x6e;
                *buf++ = 0xcf;
                /* pshufb xmm0, xmm1 */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x38;
                *buf++ = 0x00;
                *buf++ = 0xc1;
                /* movq rax, xmm0 */
                *buf++ = 0x66;
                *buf++ = 0x48;
                *buf++ = 0x0f;
                *buf++ = 0x7e;
                *buf++ = 0xc0;
                break;
#endif
            case HF64_XOR:
                /* mov rdi, imm64 */
                *buf++ = 0x48;
                *buf++ = 0xbf;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                *buf++ = ops[i].constant0 >> 32;
                *buf++ = ops[i].constant0 >> 40;
                *buf++ = ops[i].constant0 >> 48;
                *buf++ = ops[i].constant0 >> 56;
                /* xor rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x31;
                *buf++ = 0xf8;
                break;
#ifdef HAVE_CLMUL
            case HF64_CLMUL:
                /* movq xmm0, rax */
                *buf++ = 0x66;
                *buf++ = 0x48;
                *buf++ = 0x0f;
                *buf++ = 0x6e;
                *buf++ = 0xc0;
                /* mov rdi, imm64 */
                *buf++ = 0x48;
                *buf++ = 0xbf;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                *buf++ = ops[i].constant0 >> 32;
                *buf++ = ops[i].constant0 >> 40;
                *buf++ = ops[i].constant0 >> 48;
                *buf++ = ops[i].constant0 >> 56;
                /* movq xmm1, rdi */
                *buf++ = 0x66;
                *buf++ = 0x48;
                *buf++ = 0x0f;
                *buf++ = 0x6e;
                *buf++ = 0xcf;
                /* pclmulqdq xmm0, xmm1, 0x00 */
                *buf++ = 0x66;
                *buf++ = 0x0f;
                *buf++ = 0x3a;
                *buf++ = 0x44;
                *buf++ = 0xc1;
                *buf++ = 0x00;
                /* movd rax, xmm0 */
                *buf++ = 0x66;
                *buf++ = 0x48;
                *buf++ = 0x0f;
                *buf++ = 0x7e;
                *buf++ = 0xc0;
                break;
#endif
            case HF64_MUL:
                /* mov rdi, imm64 */
                *buf++ = 0x48;
                *buf++ = 0xbf;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                *buf++ = ops[i].constant0 >> 32;
                *buf++ = ops[i].constant0 >> 40;
                *buf++ = ops[i].constant0 >> 48;
                *buf++ = ops[i].constant0 >> 56;
                /* imul rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x0f;
                *buf++ = 0xaf;
                *buf++ = 0xc7;
                break;
            case HF64_ADD:
                /* mov rdi, imm64 */
                *buf++ = 0x48;
                *buf++ = 0xbf;
                *buf++ = ops[i].constant0 >>  0;
                *buf++ = ops[i].constant0 >>  8;
                *buf++ = ops[i].constant0 >> 16;
                *buf++ = ops[i].constant0 >> 24;
                *buf++ = ops[i].constant0 >> 32;
                *buf++ = ops[i].constant0 >> 40;
                *buf++ = ops[i].constant0 >> 48;
                *buf++ = ops[i].constant0 >> 56;
                /* add rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x01;
                *buf++ = 0xf8;
                break;
            case HF64_ROT:
                /* rol rax, imm8 */
                *buf++ = 0x48;
                *buf++ = 0xc1;
                *buf++ = 0xc0;
                *buf++ = ops[i].constant0;
                break;
            case HF64_XORL:
                /* mov edi, eax */
                *buf++ = 0x48;
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* shl rdi, imm8 */
                *buf++ = 0x48;
                *buf++ = 0xc1;
                *buf++ = 0xe7;
                *buf++ = ops[i].constant0;
                /* xor rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x31;
                *buf++ = 0xf8;
                break;
            case HF64_XORR:
                /* mov rdi, rax */
                *buf++ = 0x48;
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* shr rdi, imm8 */
                *buf++ = 0x48;
                *buf++ = 0xc1;
                *buf++ = 0xef;
                *buf++ = ops[i].constant0;
                /* xor rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x31;
                *buf++ = 0xf8;
                break;
            case HF64_ADDL:
                /* mov rdi, rax */
                *buf++ = 0x48;
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* shl rdi, imm8 */
                *buf++ = 0x48;
                *buf++ = 0xc1;
                *buf++ = 0xe7;
                *buf++ = ops[i].constant0;
                /* add rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x01;
                *buf++ = 0xf8;
                break;
            case HF64_SUBL:
                /* mov rdi, rax */
                *buf++ = 0x48;
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* shl rdi, imm8 */
                *buf++ = 0x48;
                *buf++ = 0xc1;
                *buf++ = 0xe7;
                *buf++ = ops[i].constant0;
                /* sub rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x29;
                *buf++ = 0xf8;
                break;
            case HF64_XROT2:
                /* mov rdi, rax */
                *buf++ = 0x48;
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* rol rdi, imm8 */
                *buf++ = 0x48;
                *buf++ = 0xc1;
                *buf++ = 0xc7;
                *buf++ = ops[i].constant0;
                /* xor rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x31;
                *buf++ = 0xf8;
                /* rol rdi, imm8 */
                *buf++ = 0x48;
                *buf++ = 0xc1;
                *buf++ = 0xc7;
                *buf++ = (32 + ops[i].constant1 - ops[i].constant0) % 32;
                /* xor rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x31;
                *buf++ = 0xf8;
                break;
        }
    }

    /* ret */
    *buf++ = 0xc3;
    return buf;
}

static void *
execbuf_alloc(void)
{
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *p = mmap(NULL, 4096, prot, flags, -1, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "prospector: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return p;
}

static enum {
    WXR_UNKNOWN, WXR_ENABLED, WXR_DISABLED
} wxr_enabled = WXR_UNKNOWN;

static void
execbuf_lock(void *buf)
{
    switch (wxr_enabled) {
        case WXR_UNKNOWN:
            if (!mprotect(buf, 4096, PROT_READ | PROT_WRITE | PROT_EXEC)) {
                wxr_enabled = WXR_DISABLED;
                return;
            }
            wxr_enabled = WXR_ENABLED;
            /* FALLTHROUGH */
        case WXR_ENABLED:
            if (mprotect(buf, 4096, PROT_READ | PROT_EXEC)) {
                fprintf(stderr,
                        "prospector: mprotect(PROT_EXEC) failed: %s\n",
                        strerror(errno));
                exit(EXIT_FAILURE);
            }
            break;
        case WXR_DISABLED:
            break;
    }
}

static void
execbuf_unlock(void *buf)
{
    switch (wxr_enabled) {
        case WXR_UNKNOWN:
            abort();
        case WXR_ENABLED:
            mprotect(buf, 4096, PROT_READ | PROT_WRITE);
            break;
        case WXR_DISABLED:
            break;
    }
}

/* Higher quality is slower but has more consistent results. */
static int score_quality = 18;

/* Measures how each input bit affects each output bit. This measures
 * both bias and avalanche.
 */
static double
estimate_bias32(uint32_t ABI (*f)(uint32_t), uint64_t rng[2])
{
    long n = 1L << score_quality;
    long bins[32][32] = {{0}};
    for (long i = 0; i < n; i++) {
        uint32_t x = xoroshiro128plus(rng);
        uint32_t h0 = f(x);
        for (int j = 0; j < 32; j++) {
            uint32_t bit = UINT32_C(1) << j;
            uint32_t h1 = f(x ^ bit);
            uint32_t set = h0 ^ h1;
            for (int k = 0; k < 32; k++)
                bins[j][k] += (set >> k) & 1;
        }
    }
    double mean = 0;
    for (int j = 0; j < 32; j++) {
        for (int k = 0; k < 32; k++) {
            /* FIXME: normalize this somehow */
            double diff = (bins[j][k] - n / 2) / (n / 2.0);
            mean += (diff * diff) / (32 * 32);
        }
    }
    return sqrt(mean) * 1000.0;
}

static double
estimate_bias64(uint64_t ABI (*f)(uint64_t), uint64_t rng[2])
{
    long n = 1L << score_quality;
    long bins[64][64] = {{0}};
    for (long i = 0; i < n; i++) {
        uint64_t x = xoroshiro128plus(rng);
        uint64_t h0 = f(x);
        for (int j = 0; j < 64; j++) {
            uint64_t bit = UINT64_C(1) << j;
            uint64_t h1 = f(x ^ bit);
            uint64_t set = h0 ^ h1;
            for (int k = 0; k < 64; k++)
                bins[j][k] += (set >> k) & 1;
        }
    }
    double mean = 0;
    for (int j = 0; j < 64; j++) {
        for (int k = 0; k < 64; k++) {
            /* FIXME: normalize this somehow */
            double diff = (bins[j][k] - n / 2) / (n / 2.0);
            mean += (diff * diff) / (64 * 64);
        }
    }
    return sqrt(mean) * 1000.0;
}

#define EXACT_SPLIT 32  // must be power of two
static double
exact_bias32(uint32_t ABI (*f)(uint32_t))
{
    long long bins[32][32] = {{0}};
    static const uint64_t range = (UINT64_C(1) << 32) / EXACT_SPLIT;
    #pragma omp parallel for
    for (int i = 0; i < EXACT_SPLIT; i++) {
        long long b[32][32] = {{0}};
        for (uint64_t x = i * range; x < (i + 1) * range; x++) {
            uint32_t h0 = f(x);
            for (int j = 0; j < 32; j++) {
                uint32_t bit = UINT32_C(1) << j;
                uint32_t h1 = f(x ^ bit);
                uint32_t set = h0 ^ h1;
                for (int k = 0; k < 32; k++)
                    b[j][k] += (set >> k) & 1;
            }
        }
        #pragma omp critical
        for (int j = 0; j < 32; j++)
            for (int k = 0; k < 32; k++)
                bins[j][k] += b[j][k];
    }
    double mean = 0.0;
    for (int j = 0; j < 32; j++) {
        for (int k = 0; k < 32; k++) {
            double diff = (bins[j][k] - 2147483648L) / 2147483648.0;
            mean += (diff * diff) / (32 * 32);
        }
    }
    return sqrt(mean) * 1000.0;
}

static void
usage(FILE *f)
{
    fprintf(f, "usage: prospector "
            "[-E|L|S] [-4|-8] [-ehs] [-l lib] [-p pattern] [-r n:m] [-t x]\n");
    fprintf(f, " -4          Generate 32-bit hash functions (default)\n");
    fprintf(f, " -8          Generate 64-bit hash functions\n");
    fprintf(f, " -e          Measure bias exactly (requires -E)\n");
    fprintf(f, " -h          Print this help message\n");
    fprintf(f, " -l ./lib.so Load hash() from a shared object\n");
    fprintf(f, " -p pattern  Search only a given pattern\n");
    fprintf(f, " -q n        Score quality knob (12-30, default: 18)\n");
    fprintf(f, " -r n:m      Use between n and m operations [3:6]\n");
    fprintf(f, " -s          Don't use large constants\n");
    fprintf(f, " -t x        Initial score threshold [10.0]\n");
    fprintf(f, " -E          Single evaluation mode (requires -p or -l)\n");
    fprintf(f, " -S          Hash function search mode (default)\n");
    fprintf(f, " -L          Enumerate output mode (requires -p or -l)\n");
}

static int
is_perm32(uint32_t vector)
{
    int mask = 0;
    for(int i = 0; i < (int)sizeof(vector); i++) {
        mask |= (1 << (uint8_t)vector);
        vector >>= 8;
    }
    return (mask == ((1 << sizeof(vector)) - 1) /* 15 */);
}

static int
is_perm64(uint64_t vector)
{
    int mask = 0;
    for(int i = 0; i < (int)sizeof(vector); i++) {
        mask |= (1 << (uint8_t)vector);
        vector >>= 8;
    }
    return (mask == ((1 << sizeof(vector)) - 1) /* 255 */);
}

static int
parse_operand(struct hf_op *op, char *buf)
{
    size_t second_operand = strcspn(buf, ":");
    char separator = buf[second_operand];
    buf[second_operand] = 0;

    op->flags |= FOP_LOCKED;
    switch (op->type) {
        case HF32_NOT:
        case HF64_NOT:
        case HF32_BSWAP:
        case HF64_BSWAP:
            return 0;
        case HF32_XOR:
#ifdef HAVE_CLMUL
        case HF32_CLMUL:
#endif
        case HF32_MUL:
        case HF32_ADD:
        case HF64_XOR:
#ifdef HAVE_CLMUL
        case HF64_CLMUL:
#endif
        case HF64_MUL:
        case HF64_ADD:
            op->constant0 = strtoull(buf, 0, 16);
            return 1;
#ifdef HAVE_SHF
        case HF32_SHF:
            op->constant0 = strtoull(buf, 0, 16);
            return is_perm32(op->constant0);
        case HF64_SHF:
            op->constant0 = strtoull(buf, 0, 16);
            return is_perm64(op->constant0);
#endif
        case HF32_ROT:
        case HF32_XORL:
        case HF32_XORR:
        case HF32_ADDL:
        case HF32_SUBL:
        case HF64_ROT:
        case HF64_XORL:
        case HF64_XORR:
        case HF64_ADDL:
        case HF64_SUBL:
            op->constant0 = atoi(buf);
            return 1;
        case HF32_XROT2:
        case HF64_XROT2:
            op->constant0 = atoi(buf);
            if (separator == ':') {
                op->constant1 = atoi(buf + second_operand + 1);
                return 1;
            } else {
                return 0;
            }
    }
    return 0;
}

static int
parse_template(struct hf_op *ops, int n, char *template, int flags)
{
    int c = 0;
    int offset = flags & F_U64 ? HF64_XOR : 0;

    for (char *tok = strtok(template, ","); tok; tok = strtok(0, ",")) {
        if (c == n) return 0;
        int found = 0;
        size_t operand = strcspn(tok, ":");
        int sep = tok[operand];
        tok[operand] = 0;
        ops[c].flags = 0;
        for (int i = 0; i < countof(hf_names); i++) {
            if (!strcmp(hf_names[i] + 2, tok)) {
                found = 1;
                ops[c].type = i + offset;
                break;
            }
        }
        if (!found)
            return 0;
        if (sep == ':' && !parse_operand(ops + c, tok + operand + 1))
            return 0;
        c++;
    }
    return c;
}

static void *
load_function(const char *so)
{
    void *handle = dlopen(so, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "prospector: could not load %s\n", so);
        exit(EXIT_FAILURE);
    }
    void *f = dlsym(handle, "hash");
    if (!f) {
        fprintf(stderr, "prospector: could not find 'hash' in %s\n", so);
        exit(EXIT_FAILURE);
    }
    return f;
}

static uint64_t
uepoch(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000LL * tv.tv_sec + tv.tv_usec;
}

int
main(int argc, char **argv)
{
    int nops = 0;
    int min = 3;
    int max = 6;
    int flags = 0;
    int use_exact = 0;
    double best = 100.0;
    char *dynamic = 0;
    char *template = 0;
    struct hf_op ops[32];
    void *buf = execbuf_alloc();
    uint64_t rng[2] = {0x2a2bc037b59ff989, 0x6d7db86fa2f632ca};

    enum {MODE_SEARCH, MODE_EVAL, MODE_LIST} mode = MODE_SEARCH;

    int option;
    while ((option = getopt(argc, argv, "48EehLl:q:r:st:p:")) != -1) {
        switch (option) {
            case '4':
                flags &= ~F_U64;
                break;
            case '8':
                flags |= F_U64;
                break;
            case 'E':
                mode = MODE_EVAL;
                break;
            case 'e':
                use_exact = 1;
                break;
            case 'h': usage(stdout);
                exit(EXIT_SUCCESS);
                break;
            case 'L':
                mode = MODE_LIST;
                break;
            case 'l':
                dynamic = optarg;
                break;
            case 'p':
                template = optarg;
                break;
            case 'r':
                if (sscanf(optarg, "%d:%d", &min, &max) != 2 ||
                    min < 1 || max > countof(ops) || min > max) {
                    fprintf(stderr, "prospector: invalid range (-r): %s\n",
                            optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'q':
                score_quality = atoi(optarg);
                if (score_quality < 12 || score_quality > 30) {
                    fprintf(stderr, "prospector: invalid quality: %s\n",
                            optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'S':
                mode = MODE_SEARCH;
                break;
            case 's':
                flags |= F_TINY;
                break;
            case 't':
                best = strtod(optarg, 0);
                break;
            default:
                usage(stderr);
                exit(EXIT_FAILURE);
        }
    }

    /* Get a unique seed */
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        if (!fread(rng, sizeof(rng), 1, urandom)) {
            fputs("prospector: failed to read /dev/urandom\n", stderr);
            exit(EXIT_FAILURE);
        }
        fclose(urandom);
    }

    if (template) {
        nops = parse_template(ops, countof(ops), template, flags);
        if (!nops) {
            fprintf(stderr, "prospector: invalid template\n");
            exit(EXIT_FAILURE);
        }
    }

    if (mode == MODE_EVAL) {
        double bias;
        void *hashptr = 0;
        if (template) {
            hf_randfunc(ops, nops, rng);
            hf_compile(ops, nops, buf);
            execbuf_lock(buf);
            hashptr = buf;
        } else if (dynamic) {
            hashptr = load_function(dynamic);
        } else {
            fprintf(stderr, "prospector: must supply -p or -l\n");
            exit(EXIT_FAILURE);
        }

        uint64_t nhash;
        uint64_t beg = uepoch();
        if (flags & F_U64) {
            uint64_t ABI (*hash)(uint64_t) = hashptr;
            if (use_exact)
                fputs("warning: no exact bias for 64-bit\n", stderr);
            bias = estimate_bias64(hash, rng);
            nhash = (1L << score_quality) * 33;
        } else {
            uint32_t ABI (*hash)(uint32_t) = hashptr;
            if (use_exact) {
                bias = exact_bias32(hash);
                nhash = (1LL << 32) * 33;
            } else {
                bias = estimate_bias32(hash, rng);
                nhash = (1L << score_quality) * 65;
            }
        }
        uint64_t end = uepoch();
        printf("bias      = %.17g\n", bias);
        printf("speed     = %.3f nsec / hash\n", (end - beg) * 1000.0 / nhash);
        return 0;
    }

    if (mode == MODE_LIST) {
        void *hashptr = 0;
        if (template) {
            hf_randfunc(ops, nops, rng);
            hf_compile(ops, nops, buf);
            execbuf_lock(buf);
            hashptr = buf;
        } else if (dynamic) {
            hashptr = load_function(dynamic);
        } else {
            fprintf(stderr, "prospector: must supply -p or -l\n");
            exit(EXIT_FAILURE);
        }

        if (flags & F_U64) {
            uint64_t ABI (*hash)(uint64_t) = hashptr;
            uint64_t i = 0;
            do
                printf("%016llx %016llx\n",
                        (unsigned long long)i,
                        (unsigned long long)hash(i));
            while (++i);
        } else {
            uint32_t ABI (*hash)(uint32_t) = hashptr;
            uint32_t i = 0;
            do
                printf("%08lx %08lx\n",
                        (unsigned long)i,
                        (unsigned long)hash(i));
            while (++i);
        }
        return 0;
    }

    for (;;) {
        /* Generate */
        if (template) {
            hf_randfunc(ops, nops, rng);
        } else {
            nops = min + xoroshiro128plus(rng) % (max - min + 1);
            hf_genfunc(ops, nops, flags, rng);
        }

        /* Evaluate */
        double score;
        hf_compile(ops, nops, buf);
        execbuf_lock(buf);
        if (flags & F_U64) {
            uint64_t ABI (*hash)(uint64_t) = (void *)buf;
            score = estimate_bias64(hash, rng);
        } else {
            uint32_t ABI (*hash)(uint32_t) = (void *)buf;
            score = estimate_bias32(hash, rng);
        }
        execbuf_unlock(buf);

        /* Compare */
        if (score < best) {
            printf("// score = %.17g\n", score);
            hf_printfunc(ops, nops, stdout);
            fflush(stdout);
            best = score;
        }
    }
}
