#include <stdint.h>

// exact bias: 0.26398543281818287
__attribute__((sysv_abi))
uint32_t
hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x85ebca6b;
    x ^= x >> 13;
    x *= 0xc2b2ae35;
    x ^= x >> 16;
    return x;
}
