/* H2 32-bit hash
 * https://github.com/h2database/h2database
 * src/test/org/h2/test/store/CalculateHashConstant.java
 */
#include <stdint.h>

// exact bias: 1.4249702882580686
__attribute__((sysv_abi))
uint32_t
hash(uint32_t x)
{
    x ^= x >> 16;
    x *= UINT32_C(0x45d9f3b);
    x ^= x >> 16;
    x *= UINT32_C(0x45d9f3b);
    x ^= x >> 16;
    return x;
}

__attribute__((sysv_abi))
uint32_t
unhash(uint32_t x)
{
    x ^= x >> 16;
    x *= UINT32_C(0x119de1f3);
    x ^= x >> 16;
    x *= UINT32_C(0x119de1f3);
    x ^= x >> 16;
    return x;
}
