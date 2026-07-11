#ifndef UR_XOR_INTERNAL_H
#define UR_XOR_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

// XOR src into dst in place: dst[i] ^= src[i] for n bytes.
// Buffers must not overlap.
void ur_xor_inplace(uint8_t *restrict dst, const uint8_t *restrict src,
                    size_t n);

// XOR two buffers into a third: out[i] = a[i] ^ b[i] for n bytes.
// Buffers must not overlap.
void ur_xor(uint8_t *restrict out, const uint8_t *restrict a,
            const uint8_t *restrict b, size_t n);

#endif // UR_XOR_INTERNAL_H
