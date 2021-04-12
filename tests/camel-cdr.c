#include <stdint.h>

__attribute__((sysv_abi))
uint64_t
hash(uint64_t idx, uint64_t mask, uint64_t seed)
{
    idx ^= seed;
    /* splittable64 */
    idx ^= (idx & mask) >> 30; idx *= UINT64_C(0xBF58476D1CE4E5B9);
    idx ^= (idx & mask) >> 27; idx *= UINT64_C(0x94D049BB133111EB);
    idx ^= (idx & mask) >> 31;
    idx *= UINT64_C(0xBF58476D1CE4E5B9);

    idx ^= seed >> 32;
    /* NOTE: This is deliberately commented out, since it doesn't improve the
     *       quality by much. */
    /* triple32 (bias = 0.020888578919738908) */
    // idx ^= (idx & mask) >> 17; idx *= UINT32_C(0xED5AD4BB);
    // idx ^= (idx & mask) >> 11; idx *= UINT32_C(0xAC4C1B51);
    // idx ^= (idx & mask) >> 15; idx *= UINT32_C(0x31848BAB);
    // idx ^= (idx & mask) >> 14;
    idx *= UINT32_C(0xED5AD4BB);

    idx ^= seed >> 48;
    /* hash16_xm3 (bias = 0.0045976709018820602) */
    idx ^= (idx & mask) >> 7; idx *= 0x2993u;
    idx ^= (idx & mask) >> 5; idx *= 0xE877u;
    idx ^= (idx & mask) >> 9; idx *= 0x0235u;
    idx ^= idx >> 10;

    /* From Andrew Kensler: "Correlated Multi-Jittered Sampling" */
    idx ^= seed >> 32; idx *= 0xe170893d;
    idx ^= seed >> 48;
    idx ^= (idx & mask) >> 4;
    idx ^= seed >> 40; idx *= 0x0929eb3f;
    idx ^= seed >> 55;
    idx ^= (idx & mask) >> 1; idx *= 1 | seed >> 59;
    idx *= 0x6935fa69;
    idx ^= (idx & mask) >> 11; idx *= 0x74dcb303;
    idx ^= (idx & mask) >> 2; idx *= 0x9e501cc3;
    idx ^= (idx & mask) >> 2; idx *= 0xc860a3df;
    idx &= mask;
    idx ^= idx >> 5;
    return idx;
}
