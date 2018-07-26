#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

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
    HF32_MUL,  // x *= const32 (odd)
    HF32_ADD,  // x += const32
    HF32_ROT,  // x  = (x << const5) | (x >> (32 - const5))
    HF32_NOT,  // x  = ~x
    HF32_XORL, // x ^= x << const5
    HF32_XORR, // x ^= x >> const5
    HF32_ADDL, // x += x << const5
    HF32_SUBL, // x -= x << const5
    /* 64 bits */
    HF64_XOR,
    HF64_MUL,
    HF64_ADD,
    HF64_ROT,
    HF64_NOT,
    HF64_XORL,
    HF64_XORR,
    HF64_ADDL,
    HF64_SUBL,
};

struct hf_op {
    enum hf_type type;
    uint64_t constant;
};

#define F_U64     (1 << 0)
#define F_TINY    (1 << 1)  // don't use big constants

static void
hf_gen(struct hf_op *op, uint64_t s[2], int flags)
{
    uint64_t r = xoroshiro128plus(s);
    int min = flags & F_TINY ? 3 : 0;
    op->type = (r % (9 - min)) + min + (flags & F_U64 ? 9 : 0);
    r >>= 4;
    switch (op->type) {
        case HF32_NOT:
        case HF64_NOT:
            op->constant = 0;
            break;
        case HF32_XOR:
        case HF32_ADD:
            op->constant = (uint32_t)r;
            break;
        case HF32_MUL:
            op->constant = (uint32_t)r | 1;
            break;
        case HF32_ROT:
        case HF32_XORL:
        case HF32_XORR:
        case HF32_ADDL:
        case HF32_SUBL:
            op->constant = 1 + r % 31;
            break;
        case HF64_XOR:
        case HF64_ADD:
            op->constant = r;
            break;
        case HF64_MUL:
            op->constant = r | 1;
            break;
        case HF64_ROT:
        case HF64_XORL:
        case HF64_XORR:
        case HF64_ADDL:
        case HF64_SUBL:
            op->constant = 1 + r % 63;
            break;
    }
}

/* Return 1 if these operations may be adjacent
*/
static int
hf_type_valid(enum hf_type a, enum hf_type b)
{
    switch (a) {
        case HF32_NOT:
        case HF32_XOR:
        case HF32_MUL:
        case HF32_ADD:
        case HF32_ROT:
        case HF64_NOT:
        case HF64_XOR:
        case HF64_MUL:
        case HF64_ADD:
        case HF64_ROT:
            return a != b;
        case HF32_XORL:
        case HF32_XORR:
        case HF32_ADDL:
        case HF32_SUBL:
        case HF64_XORL:
        case HF64_XORR:
        case HF64_ADDL:
        case HF64_SUBL:
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

static void
hf_print(const struct hf_op *op, char *buf)
{
    unsigned long long c = op->constant;
    switch (op->type) {
        case HF32_NOT:
        case HF64_NOT:
            sprintf(buf, "x  = ~x;");
            break;
        case HF32_XOR:
            sprintf(buf, "x ^= UINT32_C(0x%08llx);", c);
            break;
        case HF32_MUL:
            sprintf(buf, "x *= UINT32_C(0x%08llx);", c);
            break;
        case HF32_ADD:
            sprintf(buf, "x += UINT32_C(0x%08llx);", c);
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
        case HF64_XOR:
            sprintf(buf, "x ^= UINT64_C(0x%08llx);", c);
            break;
        case HF64_MUL:
            sprintf(buf, "x *= UINT64_C(0x%08llx);", c);
            break;
        case HF64_ADD:
            sprintf(buf, "x += UINT64_C(0x%08llx);", c);
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
        char buf[64];
        hf_print(ops + i, buf);
        fprintf(f, "    %s\n", buf);
    }
    fprintf(f, "    return x;\n}\n");
}

static unsigned char *
hf_compile(const struct hf_op *ops, int n, unsigned char *buf)
{
    /* mov eax, edi*/
    *buf++ = 0x89;
    *buf++ = 0xf8;

    for (int i = 0; i < n; i++) {
        switch (ops[i].type) {
            case HF32_NOT:
                /* not eax */
                *buf++ = 0xf7;
                *buf++ = 0xd0;
                break;
            case HF32_XOR:
                /* xor eax, imm32 */
                *buf++ = 0x35;
                *buf++ = ops[i].constant >>  0;
                *buf++ = ops[i].constant >>  8;
                *buf++ = ops[i].constant >> 16;
                *buf++ = ops[i].constant >> 24;
                break;
            case HF32_MUL:
                /* imul eax, eax, imm32 */
                *buf++ = 0x69;
                *buf++ = 0xc0;
                *buf++ = ops[i].constant >>  0;
                *buf++ = ops[i].constant >>  8;
                *buf++ = ops[i].constant >> 16;
                *buf++ = ops[i].constant >> 24;
                break;
            case HF32_ADD:
                /* add eax, imm32 */
                *buf++ = 0x05;
                *buf++ = ops[i].constant >>  0;
                *buf++ = ops[i].constant >>  8;
                *buf++ = ops[i].constant >> 16;
                *buf++ = ops[i].constant >> 24;
                break;
            case HF32_ROT:
                /* rol eax, imm8 */
                *buf++ = 0xc1;
                *buf++ = 0xc0;
                *buf++ = ops[i].constant;
                break;
            case HF32_XORL:
                /* mov edi, eax */
                *buf++ = 0x89;
                *buf++ = 0xc7;
                /* shl edi, imm8 */
                *buf++ = 0xc1;
                *buf++ = 0xe7;
                *buf++ = ops[i].constant;
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
                *buf++ = ops[i].constant;
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
                *buf++ = ops[i].constant;
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
                *buf++ = ops[i].constant;
                /* sub eax, edi */
                *buf++ = 0x29;
                *buf++ = 0xf8;
                break;
            case HF64_NOT:
                /* not rax */
                *buf++ = 0x48;
                *buf++ = 0xf7;
                *buf++ = 0xd0;
                break;
            case HF64_XOR:
                /* mov rdi, imm64 */
                *buf++ = 0x48;
                *buf++ = 0xbf;
                *buf++ = ops[i].constant >>  0;
                *buf++ = ops[i].constant >>  8;
                *buf++ = ops[i].constant >> 16;
                *buf++ = ops[i].constant >> 24;
                *buf++ = ops[i].constant >> 32;
                *buf++ = ops[i].constant >> 40;
                *buf++ = ops[i].constant >> 48;
                *buf++ = ops[i].constant >> 56;
                /* xor rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x31;
                *buf++ = 0xf8;
                break;
            case HF64_MUL:
                /* mov rdi, imm64 */
                *buf++ = 0x48;
                *buf++ = 0xbf;
                *buf++ = ops[i].constant >>  0;
                *buf++ = ops[i].constant >>  8;
                *buf++ = ops[i].constant >> 16;
                *buf++ = ops[i].constant >> 24;
                *buf++ = ops[i].constant >> 32;
                *buf++ = ops[i].constant >> 40;
                *buf++ = ops[i].constant >> 48;
                *buf++ = ops[i].constant >> 56;
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
                *buf++ = ops[i].constant >>  0;
                *buf++ = ops[i].constant >>  8;
                *buf++ = ops[i].constant >> 16;
                *buf++ = ops[i].constant >> 24;
                *buf++ = ops[i].constant >> 32;
                *buf++ = ops[i].constant >> 40;
                *buf++ = ops[i].constant >> 48;
                *buf++ = ops[i].constant >> 56;
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
                *buf++ = ops[i].constant;
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
                *buf++ = ops[i].constant;
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
                *buf++ = ops[i].constant;
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
                *buf++ = ops[i].constant;
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
                *buf++ = ops[i].constant;
                /* sub rax, rdi */
                *buf++ = 0x48;
                *buf++ = 0x29;
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
    int fd = open("/dev/zero", O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "prospector: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        fprintf(stderr, "prospector: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return p;
}

static void
execbuf_lock(void *buf)
{
    mprotect(buf, 4096, PROT_READ | PROT_EXEC);
}

static void
execbuf_unlock(void *buf)
{
    mprotect(buf, 4096, PROT_READ | PROT_WRITE);
}

double
score32(uint32_t (*f)(uint32_t), uint64_t rng[2])
{
    unsigned long long sum = 0;
    unsigned long long count = 0;
    for (long i = 0; i < 1L << 16; i++) {
        uint32_t x = xoroshiro128plus(rng);
        uint32_t h0 = f(x);
        for (int i = 0; i < 32; i++) {
            uint32_t bit = UINT32_C(1) << i;
            uint32_t h1 = f(x ^ bit);
            sum += abs(__builtin_popcount(h0 ^ h1) - 16);
            count++;
        }
    }
    return sum / (double)count;
}

double
score64(uint64_t (*f)(uint64_t), uint64_t rng[2])
{
    unsigned long long sum = 0;
    unsigned long long count = 0;
    for (long i = 0; i < 1L << 16; i++) {
        uint64_t x = xoroshiro128plus(rng);
        uint64_t h0 = f(x);
        for (int i = 0; i < 64; i++) {
            uint64_t bit = UINT64_C(1) << i;
            uint64_t h1 = f(x ^ bit);
            sum += abs(__builtin_popcountl(h0 ^ h1) - 32);
            count++;
        }
    }
    return sum / (double)count;
}

#if 0
/* Exhaustively check every pair that differs by one bit.
 * This function is very slow, so it's not used.
 */
double
score32_full(uint32_t (*f)(uint32_t))
{
    unsigned long long sum = 0;
    unsigned long long count = 0;
    uint32_t x = 0;
    do {
        uint32_t h0 = f(x);
        for (int i = 0; i < 32; i++) {
            uint32_t bit = UINT32_C(1) << i;
            uint32_t s = x | bit;
            if (s != x) {
                uint32_t h1 = f(s);
                int c = abs(__builtin_popcountl(h0 ^ h1) - 16);
                sum += c;
                count++;
            }
        }
        x++;
    } while (x);
    return sum / (double)count;
}
#endif

static void
usage(FILE *f)
{
    fprintf(f, "usage: prospector [-8hs] [-r n:m]\n");
    fprintf(f, "  -8      Generate 64-bit hash functions (default: 32-bit)\n");
    fprintf(f, "  -h      Print this help message\n");
    fprintf(f, "  -s      Don't use large constants\n");
    fprintf(f, "  -r n:m  Use between n and m operations (default: 3:6)\n");
}

int
main(int argc, char **argv)
{
    int min = 3;
    int max = 6;
    int flags = 0;
    double best = 16.0;
    void *buf = execbuf_alloc();
    uint64_t rng[2] = {0x2a2bc037b59ff989, 0x6d7db86fa2f632ca};

    int option;
    while ((option = getopt(argc, argv, "8hsr:")) != -1) {
        switch (option) {
            case '8':
                flags |= F_U64;
                break;
            case 'r':
                if (sscanf(optarg, "%d:%d", &min, &max) != 2 ||
                    min < 1 || max > 32 || min > max) {
                    fprintf(stderr, "prospector: invalid range (-r): %s\n",
                            optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 's':
                flags |= F_TINY;
                break;
            case 'h':
                usage(stdout);
                exit(EXIT_SUCCESS);
                break;
            default:
                usage(stderr);
                exit(EXIT_FAILURE);
        }
    }

    /* Get a unique seed */
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        fread(rng, 1, sizeof(rng), urandom);
        fclose(urandom);
    }

    for (;;) {
        /* Generate */
        struct hf_op ops[32];
        int n = min + xoroshiro128plus(rng) % (max - min + 1);
        hf_genfunc(ops, n, flags, rng);

        /* Evaluate */
        double score;
        hf_compile(ops, n, buf);
        execbuf_lock(buf);
        if (flags & F_U64) {
            uint64_t (*hash)(uint64_t) = (void *)buf;
            score = score64(hash, rng);
        } else {
            uint32_t (*hash)(uint32_t) = (void *)buf;
            score = score32(hash, rng);
        }
        execbuf_unlock(buf);

        /* Compare */
        if (score < best) {
            printf("// score = %.17g\n", score);
            hf_printfunc(ops, n, stdout);
            fflush(stdout);
            best = score;
        }
    }
}
