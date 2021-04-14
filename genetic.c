/* Genetic algorithm to explore xorshift-multiply-xorshift hashes.
 */
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define POOL      40
#define THRESHOLD 2.0  // Use exact when estimate is below this
#define DONTCARE  0.3  // Only print tuples with bias below this threshold
#define QUALITY   18   // 2^N iterations of estimate samples
#define RESETMINS 90   // Reset pool after this many minutes of no progress

static uint64_t
rand64(uint64_t s[4])
{
    uint64_t x = s[1] * 5;
    uint64_t r = ((x << 7) | (x >> 57)) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = (s[3] << 45) | (s[3] >> 19);
    return r;
}


#define FLAG_SCORED  (1u << 0)
#define FLAG_EXACT   (1u << 1)
#define FLAG_PRINTED (1u << 2)

struct gene {
    double score;
    short s[3];
    uint32_t c[2];
    unsigned flags;
};

static uint32_t
hash(const struct gene *g, uint32_t x)
{
    x ^= x >> g->s[0];
    x *= g->c[0];
    x ^= x >> g->s[1];
    x *= g->c[1];
    x ^= x >> g->s[2];
    return x;
}

static double
estimate_bias32(const struct gene *g, uint64_t rng[4])
{
    long n = 1L << QUALITY;
    long bins[32][32] = {{0}};
    for (long i = 0; i < n; i++) {
        uint32_t x = rand64(rng);
        uint32_t h0 = hash(g, x);
        for (int j = 0; j < 32; j++) {
            uint32_t bit = UINT32_C(1) << j;
            uint32_t h1 = hash(g, x ^ bit);
            uint32_t set = h0 ^ h1;
            for (int k = 0; k < 32; k++)
                bins[j][k] += (set >> k) & 1;
        }
    }
    double mean = 0;
    for (int j = 0; j < 32; j++) {
        for (int k = 0; k < 32; k++) {
            double diff = (bins[j][k] - n / 2) / (n / 2.0);
            mean += (diff * diff) / (32 * 32);
        }
    }
    return sqrt(mean) * 1000.0;
}

static double
exact_bias32(const struct gene *g)
{
    long long bins[32][32] = {{0}};
    static const uint64_t range = (UINT64_C(1) << 32);
    #pragma omp parallel for reduction(+:bins[:32][:32])
    for (uint64_t x = 0; x < range; x++) {
        uint32_t h0 = hash(g, x);
        for (int j = 0; j < 32; j++) {
            uint32_t bit = UINT32_C(1) << j;
            uint32_t h1 = hash(g, x ^ bit);
            uint32_t set = h0 ^ h1;
            for (int k = 0; k < 32; k++)
                bins[j][k] += (set >> k) & 1;
        }
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
gene_gen(struct gene *g, uint64_t rng[4])
{
    uint64_t s = rand64(rng);
    uint64_t c = rand64(rng);
    g->s[0] = 10 + (s >>  0) % 10;
    g->s[1] = 10 + (s >> 24) % 10;
    g->s[2] = 10 + (s >> 48) % 10;
    g->c[0] = c | 1u;
    g->c[1] = (c >> 32) | 1u;
    g->flags = 0;
}

static void
gene_print(const struct gene *g, FILE *f)
{
    fprintf(f, "[%2d %08lx %2d %08lx %2d]",
            g->s[0], (unsigned long)g->c[0],
            g->s[1], (unsigned long)g->c[1], g->s[2]);
}

static int
small(uint64_t r)
{
    static const int v[] = {-3, -2, -1, +1, +2, +3};
    return v[r % 6];
}

static void
gene_mutate(struct gene *g, uint64_t rng[4])
{
    uint64_t r = rand64(rng);
    int s = r % 5;
    r >>= 3;
    switch (s) {
        case 0:
            g->s[0] += small(r);
            break;
        case 1:
            g->s[1] += small(r);
            break;
        case 2:
            g->s[2] += small(r);
            break;
        case 3:
            g->c[0] += (int)(r & 0xffff) - 32768;
            break;
        case 4:
            g->c[1] += (int)(r & 0xffff) - 32768;
            break;
    }
    g->flags = 0;
}

static void
gene_cross(struct gene *g,
           const struct gene *a,
           const struct gene *b,
           uint64_t rng[4])
{
    uint64_t r = rand64(rng);
    *g = *a;
    switch (r & 2) {
        case 0: g->c[0] = b->c[0]; /* FALLTHROUGH */
        case 1: g->s[1] = b->s[1]; /* FALLTHROUGH */
        case 2: g->c[1] = b->c[1]; /* FALLTHROUGH */
        case 3: g->s[2] = b->s[2];
    }
    g->flags = 0;
}

static int
gene_same(const struct gene *a, const struct gene *b)
{
    return a->s[0] == b->s[0] &&
           a->s[1] == b->s[1] &&
           a->s[2] == b->s[2] &&
           a->c[0] == b->c[0] &&
           a->c[1] == b->c[1];
}

static void
rng_init(void *p, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f)
        abort();
    if (!fread(p, 1, len, f))
        abort();
    fclose(f);
}

static int
cmp(const void *pa, const void *pb)
{
    double a = *(double *)pa;
    double b = *(double *)pb;
    if (a < b)
        return -1;
    if (b < a)
        return 1;
    return 0;
}

static void
undup(struct gene *pool, uint64_t rng[4])
{
    for (int i = 0; i < POOL; i++)
        for (int j = i + 1; j < POOL; j++)
            if (gene_same(pool + i, pool + j))
                gene_mutate(pool + j, rng);
}

int
main(void)
{
    int verbose = 1;
    double best = 1000.0;
    time_t best_time = time(0);
    uint64_t rng[POOL][4];
    struct gene pool[POOL];

    rng_init(rng, sizeof(rng));
    for (int i = 0; i < POOL; i++)
        gene_gen(pool + i, rng[0]);

    for (;;) {
        #pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < POOL; i++) {
            if (!(pool[i].flags & FLAG_SCORED)) {
                pool[i].score = estimate_bias32(pool + i, rng[i]);
                pool[i].flags |= FLAG_SCORED;
            }
        }
        for (int i = 0; i < POOL; i++) {
            if (!(pool[i].flags & FLAG_EXACT) && pool[i].score < THRESHOLD) {
                pool[i].score = exact_bias32(pool + i);
                pool[i].flags |= FLAG_EXACT;
            }
        }

        qsort(pool, POOL, sizeof(*pool), cmp);
        if (verbose) {
            for (int i = 0; i < POOL; i++) {
                if (!(pool[i].flags & FLAG_PRINTED) &&
                      pool[i].score < DONTCARE) {
                    gene_print(pool + i, stdout);
                    printf(" = %.17g\n", pool[i].score);
                    pool[i].flags |= FLAG_PRINTED;
                }
            }
        }

        time_t now = time(0);
        if (pool[0].score < best) {
            best = pool[0].score;
            best_time = now;
        } else if (now - best_time > RESETMINS * 60) {
            best = 1000.0;
            best_time = now;
            for (int i = 0; i < POOL; i++)
                gene_gen(pool + i, rng[0]);
        }

        int c = POOL / 4;
        for (int a = 0; c < POOL && a < POOL / 4; a++)
            for (int b = a + 1; c < POOL && b < POOL / 4; b++)
                gene_cross(pool + c++, pool + a, pool + b, rng[0]);
        undup(pool, rng[0]);
    }
}
