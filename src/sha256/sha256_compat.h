#ifndef UR_SHA256_COMPAT_H
#define UR_SHA256_COMPAT_H

#include <stddef.h>
#include <stdint.h>

// When UR_USE_MBEDTLS_SHA256 is defined (e.g. on ESP-IDF), route to
// mbedtls so targets with hardware-accelerated SHA256 use it. Otherwise
// fall back to the bundled implementation in sha256/sha256.c.
#ifdef UR_USE_MBEDTLS_SHA256

#include <mbedtls/sha256.h>

static inline void ur_sha256(const uint8_t *input, size_t len,
                             uint8_t output[32]) {
  mbedtls_sha256(input, len, output, 0);
}

#else

#include "sha256/sha256.h"

static inline void ur_sha256(const uint8_t *input, size_t len,
                             uint8_t output[32]) {
  CRYAL_SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, input, len);
  sha256_final(&ctx, output);
}

#endif

#endif
