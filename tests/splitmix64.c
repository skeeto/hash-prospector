#include <stdint.h>

__attribute__((sysv_abi))
uint64_t
hash(uint64_t x)
{
    x += 0x9e3779b97f4a7c15;
    x ^= (x >> 30);
    x *= 0xbf58476d1ce4e5b9;
    x ^= (x >> 27);
    x *= 0x94d049bb133111eb;
    x ^= (x >> 31);
    return x;
}
