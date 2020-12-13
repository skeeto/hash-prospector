/* H2 32-bit hash
 * https://github.com/h2database/h2database
 * src/test/org/h2/test/store/CalculateHashConstant.java
 */
#include <stdint.h>

__attribute__((sysv_abi))
uint64_t
hash(uint64_t x)
{
    x ^= x >> 32;
    x *= 0xd6e8feb86659fd93;
    x ^= x >> 32;
    x *= 0xd6e8feb86659fd93;
    x ^= x >> 32;
    return x;
}

__attribute__((sysv_abi))
uint64_t
unhash(uint64_t x)
{
    x ^= x >> 32;
    x *= 0xcfee444d8b59a89b;
    x ^= x >> 32;
    x *= 0xcfee444d8b59a89b;
    x ^= x >> 32;
    return x;
}
