#include <stdint.h>

__attribute__((sysv_abi))
uint64_t
hash(uint64_t idx, uint64_t mask, uint64_t seed)
{
    /* From Andrew Kensler: "Correlated Multi-Jittered Sampling" */
    idx ^= seed; idx *= 0xe170893d;
    idx ^= seed >> 16;
    idx ^= (idx & mask) >> 4;
    idx ^= seed >> 8; idx *= 0x0929eb3f;
    idx ^= seed >> 23;
    idx ^= (idx & mask) >> 1; idx *= 1 | seed >> 27;
    idx *= 0x6935fa69;
    idx ^= (idx & mask) >> 11; idx *= 0x74dcb303;
    idx ^= (idx & mask) >> 2; idx *= 0x9e501cc3;
    idx ^= (idx & mask) >> 2; idx *= 0xc860a3df;
    idx &= mask;
    idx ^= idx >> 5;
    return idx;
}
