#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <omp.h>

#include <dlfcn.h>
#include <unistd.h>
#include <sys/time.h>

#define ABI __attribute__((sysv_abi))

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


#define F_U64     (1 << 0)
#define F_TINY    (1 << 1)  // don't use big constants

/* Higher quality is slower but has more consistent results. */
static int score_quality = 16;
static int nbits = 32;
static int full_range = 0;

/* Measures how each input bit affects each output bit. This measures
 * both bias and avalanche.
 */


static double
estimate_bias(uint64_t (*hash)(uint64_t idx, uint64_t mask, uint64_t seed),
              int bits, uint64_t rng[2])
{
    uint64_t mask = (UINT64_C(1) << bits) - 1;
    if (bits == 64)
        mask = UINT64_MAX;

    long n = 1L << score_quality;
    /* we treat the index and the seed together as the input */
    long bins[128][64] = {{0}};

    int range = full_range ? nbits : bits;

    for (long i = 0; i < n; i++) {
        uint64_t seed = xoroshiro128plus(rng);
        uint64_t x = xoroshiro128plus(rng) & mask;
        uint64_t h0 = hash(x, mask, seed);
        /* evaluate seed changes */
        for (int j = 0; j < range; j++) {
            uint64_t bit = UINT64_C(1) << j;
            uint64_t h1 = hash(x, mask, seed ^ bit);
            uint64_t set = h0 ^ h1;
            for (int k = 0; k < bits; k++)
                bins[j][k] += (set >> k) & 1;
        }
        /* evaluate index changes */
        for (int j = 0; j < bits; j++) {
            uint64_t bit = UINT64_C(1) << j;
            uint64_t h1 = hash(x ^ bit, mask, seed);
            uint64_t set = h0 ^ h1;
            for (int k = 0; k < bits; k++)
                bins[j + range][k] += (set >> k) & 1;
        }
    }

    double mean = 0;
    for (int j = 0; j < bits + range; j++) {
        for (int k = 0; k < bits; k++) {
            /* FIXME: normalize this somehow */
            double diff = (bins[j][k] - n / 2) / (n / 2.0);
            mean += (diff * diff) / ((bits + range) * bits);
        }
    }

    return sqrt(mean) * 1000.0;
}


static void
usage(FILE *f)
{
    fprintf(f, "usage: evalpow2 "
            "[-E|L|S] [-4|-8] [-ehs] [-l lib] [-p pattern] [-r n:m] [-t x]\n");
    fprintf(f, " -f          Evaluate the full seed (makes comparing hashes harder)\n");
    fprintf(f, "             [default: only up to the current power-of-two]\n");
    fprintf(f, " -h          Print this help message\n");
    fprintf(f, " -v          Print the bias for every power-of-two tested\n");
    fprintf(f, " -l ./lib.so Load hash() from a shared object\n");
    fprintf(f, " -p pattern  Search only a given pattern\n");
    fprintf(f, " -q x        Score quality knob (12-30, default: 16)\n");
    fprintf(f, " -n n        Test all powers of two up to 2^n [32]\n");
}

static void *
load_function(const char *so)
{
    void *handle = dlopen(so, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "evalpow2: could not load %s\n", so);
        exit(EXIT_FAILURE);
    }
    void *f = dlsym(handle, "hash");
    if (!f) {
        fprintf(stderr, "evalpow2: could not find 'hash' in %s\n", so);
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
    int verbose = 0;
    char *dynamic = 0;
    uint64_t rng[2] = {0x2a2bc037b59ff989, 0x6d7db86fa2f632ca};

    int option;
    while ((option = getopt(argc, argv, "fhvl:n:q:")) != -1) {
        switch (option) {
            case 'f':
                full_range = 1;
                break;
            case 'h': usage(stdout);
                exit(EXIT_SUCCESS);
                break;
            case 'v':
                verbose = 1;
                break;
            case 'l':
                dynamic = optarg;
                break;
            case 'n':
                nbits = atoi(optarg);
                break;
            case 'q':
                score_quality = atoi(optarg);
                if (score_quality < 12 || score_quality > 30) {
                    fprintf(stderr, "evalpow2: invalid quality: %s\n",
                            optarg);
                    exit(EXIT_FAILURE);
                }
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
            fputs("evalpow2: failed to read /dev/urandom\n", stderr);
            exit(EXIT_FAILURE);
        }
        fclose(urandom);
    }

    uint64_t ABI (*hash)(uint64_t,uint64_t,uint64_t) = load_function(dynamic);

    double total = 0;
    uint64_t nhash = 0;
    uint64_t time = 0;

    for (int i = 1; i < nbits; ++i) {
        uint64_t beg = uepoch();
        double bias = estimate_bias(hash, i, rng);
        time += (uepoch() - beg);
        if (verbose)
            printf("bias %2d: %.17g\n", i, bias);
        total += bias;
        nhash += (1L << score_quality) * (i+(full_range?nbits:i)+1);
    }

    printf("total bias = %.17g\n", total);
    printf("avr bias   = %.17g\n", total / nbits);
    printf("speed      = %.3f nsec / hash\n", time * 1000.0 / nhash);
    return 0;
}
