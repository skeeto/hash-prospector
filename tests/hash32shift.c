#include <stdint.h>

uint32_t
hash(uint32_t x)
{
    x  = ~x + (x << 15);
    x ^= x >> 12;
    x += x << 2;
    x ^= x >> 4;
    x *= 2057;
    x ^= x >> 16;
    return x;
}
