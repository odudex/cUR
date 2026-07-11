#ifndef UR_SHA256_COMPAT_H
#define UR_SHA256_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// Pick a SHA256 backend based on a compile-time flag.
//   UR_USE_MBEDTLS_SHA256 -> mbedTLS PSA hash (e.g. ESP-IDF)
//   UR_USE_K210_SHA256    -> sha256_hard_calculate (Kendryte K210 SDK)
// Otherwise, fall back to the bundled implementation in sha256/sha256.c.
#if defined(UR_USE_MBEDTLS_SHA256)

#include <psa/crypto.h>

static inline void ur_sha256(const uint8_t *input, size_t len,
                             uint8_t output[32]) {
  size_t hash_len = 0;
  if (psa_crypto_init() != PSA_SUCCESS) {
    abort();
  }
  if (psa_hash_compute(PSA_ALG_SHA_256, input, len, output, 32, &hash_len) !=
          PSA_SUCCESS ||
      hash_len != 32) {
    abort();
  }
}

#elif defined(UR_USE_K210_SHA256)

#include <sha256.h>

static inline void ur_sha256(const uint8_t *input, size_t len,
                             uint8_t output[32]) {
  sha256_hard_calculate(input, len, output);
}

#else

#include "sha256/sha256.h"

static inline void ur_sha256(const uint8_t *input, size_t len,
                             uint8_t output[32]) {
  CRYAL_SHA256_CTX ctx;
  ur_bundled_sha256_init(&ctx);
  ur_bundled_sha256_update(&ctx, input, len);
  ur_bundled_sha256_final(&ctx, output);
}

#endif

#endif
