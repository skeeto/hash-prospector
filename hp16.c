/* 16-bit hash prospector
 *
 * Unlike the 32-bit / 64-bit prospector, this implementation is fully
 * portable and will run on just about any system. It's also capable of
 * generating and evaluating 128kB s-boxes.
 *
 * Be mindful of C integer promotion rules when doing 16-bit operations.
 * For instance, on 32-bit implementations unsigned 16-bit operands will
 * be promoted to signed 32-bit integers, leading to incorrect results in
 * certain cases. The C programs printed by this program are careful to
 * promote 16-bit operations to "unsigned int" where needed.
 *
 * Since 16-bit hashes are likely to be needed on machines that do not
 * have efficient hardware multiplication or whose ISAs lack rotation
 * instructions, these operations may be optionally omitted during
 * exploration (-m, -r).
 *
 * This is free and unencumbered software released into the public domain.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OPS_MAX 32

enum hf_type {
    HF16_XOR,   // x ^= imm
    HF16_MUL,   // x *= imm (odd)
    HF16_ADD,   // x += imm
    HF16_ROT,   // x  = (x << imm) | (x >> (16 - imm))
    HF16_NOT,   // x  = ~x
    HF16_XORL,  // x ^= x << imm
    HF16_XORR,  // x ^= x >> imm
    HF16_ADDL,  // x += x << imm
    HF16_SUBL,  // x -= x << imm
    HF16_SBOX,  // x  = sbox[x]
};

struct hf_op {
    enum hf_type type;
    unsigned imm;
};

static unsigned short sbox[1L<<16];

static unsigned long long
hash64(unsigned long long x)
{
    x ^= x >> 32;
    x *= 0x25b751109e05be63;
    x &= 0xffffffffffffffff;
    x ^= x >> 32;
    x *= 0x2330e1453ed4b9b9;
    x &= 0xffffffffffffffff;
    x ^= x >> 32;
    return x;
}

static unsigned long
u32(unsigned long long *s)
{
    unsigned long r = *s >> 32;
    *s = *s*0x7c3c3267d015ceb5 + 1;
    r &= 0xffffffff;
    r ^= r >> 16;
    r *= 0x60857ba9;
    return r & 0xffffffff;
}

static unsigned long
randint(unsigned long r, unsigned long long s[1])
{
    unsigned long long x = u32(s);
    unsigned long long m = x * r;
    unsigned long y = m & 0xffffffff;
    if (y < r) {
        unsigned long t = -r % r;
        while (y < t) {
            x = u32(s);
            m = x * r;
            y = m & 0xffffffff;
        }
    }
    return m >> 32;
}

static struct hf_op
hf_gen(enum hf_type type, unsigned long long s[1])
{
    struct hf_op op;
    op.type = type;
    switch (op.type) {
    case HF16_NOT:
    case HF16_SBOX: op.imm = 0; break;
    case HF16_XOR:
    case HF16_ADD:  op.imm = u32(s)>>16; break;
    case HF16_MUL:  op.imm = u32(s)>>16 | 1; break;
    case HF16_ROT:
    case HF16_XORL:
    case HF16_XORR:
    case HF16_ADDL:
    case HF16_SUBL: op.imm = 1 + u32(s)%15; break;
    }
    return op;
}

/* May these operations be adjacent? */
static int
hf_type_valid(enum hf_type a, enum hf_type b)
{
    switch (a) {
    case HF16_NOT:
    case HF16_XOR:
    case HF16_MUL:
    case HF16_ADD:
    case HF16_ROT:
    case HF16_SBOX: return a != b;
    case HF16_XORL:
    case HF16_XORR:
    case HF16_ADDL:
    case HF16_SUBL: return 1;
    }
    return 0;
}

static void
hf_genfunc(struct hf_op *ops, int n, unsigned long long s[1])
{
    for (int i = 0; i < n; i++) {
        do {
            enum hf_type type = u32(s) % HF16_SBOX;  // (exclude sbox)
            ops[i] = hf_gen(type, s);
        } while (i > 0 && !hf_type_valid(ops[i-1].type, ops[i].type));
    }
}

/* Indicate operation mixing direction (+1 left, 0 none, -1 right). */
static int
opdir(struct hf_op op)
{
    switch (op.type) {
    case HF16_NOT:
    case HF16_XOR:
    case HF16_ADD:
    case HF16_SBOX: return 0;
    case HF16_MUL:
    case HF16_XORL:
    case HF16_ADDL:
    case HF16_SUBL: return +1;
    case HF16_XORR: return -1;
    case HF16_ROT:  if (op.imm < 8) return +1;
                    if (op.imm > 8) return -1;
                    return 0;
    }
    abort();
}

/* Prefer to alternate bit mixing directions. */
static void
hf_gensmart(struct hf_op *ops, int n, unsigned long long s[1])
{
    int dir = 0;
    for (int i = 0; i < n; i++) {
        int newdir;
        do {
            ops[i] = hf_gen(u32(s)%HF16_SBOX, s);
            newdir = opdir(ops[i]);
        } while (dir && newdir == dir);
        dir = newdir ? newdir : dir;
    }
}

static void
hf_genxormul(struct hf_op *ops, int n, unsigned long long s[1])
{
    ops[0].type = HF16_XORR;
    ops[0].imm = 1 + u32(s)%15;
    for (int i = 0; i < n; i++) {
        ops[2*i+1].type = HF16_MUL;
        ops[2*i+1].imm = u32(s)>>16 | 1;
        ops[2*i+2].type = HF16_XORR;
        ops[2*i+2].imm = 1 + u32(s)%15;
    }
}

static unsigned
hf_apply(const struct hf_op *ops, int n, unsigned x)
{
    for (int i = 0; i < n; i++) {
        switch (ops[i].type) {
        case HF16_XOR:  x ^= ops[i].imm; break;
        case HF16_MUL:  x *= ops[i].imm; break;
        case HF16_ADD:  x += ops[i].imm; break;
        case HF16_ROT:  x  = x<<ops[i].imm | x>>(16 - ops[i].imm); break;
        case HF16_NOT:  x  = ~x; break;
        case HF16_XORL: x ^= x << ops[i].imm; break;
        case HF16_XORR: x ^= x >> ops[i].imm; break;
        case HF16_ADDL: x += x << ops[i].imm; break;
        case HF16_SUBL: x -= x << ops[i].imm; break;
        case HF16_SBOX: x  = sbox[x]; break;
        }
        x &= 0xffff;
    }
    return x;
}

static void
hf_print(const struct hf_op *ops, int n, FILE *f)
{
    fprintf(f, "uint16_t hash(uint16_t x)\n");
    fprintf(f, "{\n");
    for (int i = 0; i < n; i++) {
        fputs("    ", f);
        switch (ops[i].type) {
        case HF16_XOR:
            fprintf(f, "x ^= 0x%04x;\n", ops[i].imm);
            break;
        case HF16_MUL:
            fprintf(f, "x *= 0x%04xU;\n", ops[i].imm);
            break;
        case HF16_ADD:
            fprintf(f, "x += 0x%04xU;\n", ops[i].imm);
            break;
        case HF16_ROT:
            fprintf(f, "x  = (unsigned)x<<%d | x >>%d;\n",
                    ops[i].imm, 16-ops[i].imm);
            break;
        case HF16_NOT:
            fprintf(f, "x  = ~x;\n");
            break;
        case HF16_XORL:
            fprintf(f, "x ^= (unsigned)x << %d;\n", ops[i].imm);
            break;
        case HF16_XORR:
            fprintf(f, "x ^= x >> %d;\n", ops[i].imm);
            break;
        case HF16_ADDL:
            fprintf(f, "x += (unsigned)x << %d;\n", ops[i].imm);
            break;
        case HF16_SUBL:
            fprintf(f, "x -= (unsigned)x << %d;\n", ops[i].imm);
            break;
        case HF16_SBOX:
            fprintf(f, "x  = sbox[x];\n");
            break;
        }
    }
    fprintf(f, "    return x;\n");
    fprintf(f, "}\n");
}

static void
sbox_init(void)
{
    for (long i = 0; i < 1L<<16; i++) {
        sbox[i] = i;
    }
}

static void
sbox_shuffle(unsigned long long s[1])
{
    for (long i = 0xffff; i > 0; i--) {
        long j = randint(i + 1, s);
        unsigned swap = sbox[i];
        sbox[i] = sbox[j];
        sbox[j] = swap;
    }
}

static void
sbox_print(FILE *f)
{
    for (long i = 0; i < 1L<<16; i++) {
        fprintf(f, "%04x%c", sbox[i], i % 16 == 15 ? '\n' : ' ');
    }
}

static double
score(const struct hf_op *ops, int n)
{
    long bins[32][32] = {{0}};
    for (long x = 0; x < 1L<<16; x++) {
        unsigned h0 = hf_apply(ops, n, x);
        for (int j = 0; j < 16; j++) {
            unsigned bit = 1U << j;
            unsigned h1 = hf_apply(ops, n, x^bit);
            unsigned set = h0 ^ h1;
            for (int k = 0; k < 16; k++)
                bins[j][k] += (set >> k) & 1;
        }
    }

    double mean = 0.0;
    for (int j = 0; j < 16; j++) {
        for (int k = 0; k < 16; k++) {
            double diff = (bins[j][k] - (1<<15)) / (double)(1<<15);
            mean += (diff * diff) / (16 * 16);
        }
    }
    return sqrt(mean);
}

static int
match(const struct hf_op *ops, int n, int types)
{
    for (int i = 0; i < n; i++) {
        if (1<<ops[i].type & types) {
            return 1;
        }
    }
    return 0;
}

static int xoptind = 1;
static int xopterr = 1;
static int xoptopt;
static char *xoptarg;

static int
xgetopt(int argc, char **argv, const char *optstring)
{
    static int optpos = 1;
    const char *arg;
    (void)argc;

    /* Reset? */
    if (xoptind == 0) {
        xoptind = 1;
        optpos = 1;
    }

    arg = argv[xoptind];
    if (arg && strcmp(arg, "--") == 0) {
        xoptind++;
        return -1;
    } else if (!arg || arg[0] != '-' || !isalnum(arg[1])) {
        return -1;
    } else {
        const char *opt = strchr(optstring, arg[optpos]);
        xoptopt = arg[optpos];
        if (!opt) {
            if (xopterr && *optstring != ':')
                fprintf(stderr, "%s: illegal option: %c\n", argv[0], xoptopt);
            return '?';
        } else if (opt[1] == ':') {
            if (arg[optpos + 1]) {
                xoptarg = (char *)arg + optpos + 1;
                xoptind++;
                optpos = 1;
                return xoptopt;
            } else if (argv[xoptind + 1]) {
                xoptarg = (char *)argv[xoptind + 1];
                xoptind += 2;
                optpos = 1;
                return xoptopt;
            } else {
                if (xopterr && *optstring != ':')
                    fprintf(stderr,
                            "%s: option requires an argument: %c\n",
                            argv[0], xoptopt);
                return *optstring == ':' ? ':' : '?';
            }
        } else {
            if (!arg[++optpos]) {
                xoptind++;
                optpos = 1;
            }
            return xoptopt;
        }
    }
}

static void
usage(FILE *f)
{
    fprintf(f, "hp16: [-HISX] [-hmr] [-n INT]\n");
    fprintf(f, "  -H     mode: random hash prospector (default)\n");
    fprintf(f, "  -I     mode: smarter (?) hash prospector\n");
    fprintf(f, "  -S     mode: s-box prospector \n");
    fprintf(f, "  -X     mode: xorshift-multiply prospector\n");
    fprintf(f, "  -h     print this message and exit\n");
    fprintf(f, "  -m     exclude multiplication\n");
    fprintf(f, "  -n INT number of operations\n");
    fprintf(f, "  -r     exclude rotation\n");
}

int
main(int argc, char **argv)
{
    char *ptr;
    int n = 0;
    int exclude = 0;
    enum {MODE_HASH, MODE_SMART, MODE_XORMUL, MODE_SBOX} mode = MODE_HASH;
    unsigned long tmp;
    struct hf_op ops[1+2*OPS_MAX] = {{HF16_SBOX, 0}};

    int option;
    while ((option = xgetopt(argc, argv, "HhImn:rSX")) != -1) {
        switch (option) {
        case 'H':
            mode = MODE_HASH;
            break;
        case 'h':
            usage(stdout);
            return 0;
        case 'I':
            mode = MODE_SMART;
            break;
        case 'm':
            exclude |= 1<<HF16_MUL;
            break;
        case 'n':
            tmp = strtoul(xoptarg, &ptr, 10);
            if (!tmp || *ptr || tmp > OPS_MAX) {
                fprintf(stderr, "fatal: invalid n, %s\n", xoptarg);
                usage(stderr);
                return 1;
            }
            n = tmp;
            break;
        case 'r':
            exclude |= 1<<HF16_ROT;
            break;
        case 'S':
            mode = MODE_SBOX;
            break;
        case 'X':
            mode = MODE_XORMUL;
            break;
        case '?':
            usage(stderr);
            return 1;
        }
    }

    switch (mode) {
    case MODE_HASH:
    case MODE_SMART:  n = n ? n : 7; break;
    case MODE_XORMUL: n = n ? 1 + 2*n : 5; break;
    case MODE_SBOX:   sbox_init(); n = 1; break;
    }

    double best = 1;
    unsigned long long s[1] = {hash64(time(0))};

    for (;;) {
        *s += hash64(time(0));
        switch (mode) {
        case MODE_HASH:
            do {
                hf_genfunc(ops, n, s);
            } while (match(ops, n, exclude));
            break;
        case MODE_SMART:
            do {
                hf_gensmart(ops, n, s);
            } while (match(ops, n, exclude));
            break;
        case MODE_XORMUL:
            hf_genxormul(ops, (n-1)/2, s);
            break;
        case MODE_SBOX:
            sbox_shuffle(s);
            break;
        }
        *s -= hash64(clock());

        double r = score(ops, n);
        if (r < best) {
            switch (mode) {
            case MODE_HASH:
            case MODE_SMART:
            case MODE_XORMUL:
                printf("// bias = %.17g\n", r);
                hf_print(ops, n, stdout);
                fputc('\n', stdout);
                break;
            case MODE_SBOX:
                fprintf(stdout, "// bias = %.17g\n", r);
                sbox_print(stdout);
                fputc('\n', stdout);
                fprintf(stderr, "// bias = %.17g\n", r);
                fflush(stderr);
                break;
            }
            fflush(stdout);
            best = r;
        }
    }
}
