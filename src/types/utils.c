#include "utils.h"
#include "../sha256/sha256.h"
#include <stdlib.h>
#include <string.h>

// Byte buffer utilities
byte_buffer_t *byte_buffer_new(void) {
  return byte_buffer_new_with_capacity(16);
}

byte_buffer_t *byte_buffer_new_with_capacity(size_t capacity) {
  byte_buffer_t *buf = safe_malloc(sizeof(byte_buffer_t));
  if (!buf)
    return NULL;

  buf->data = safe_malloc(capacity);
  if (!buf->data) {
    free(buf);
    return NULL;
  }

  buf->len = 0;
  buf->capacity = capacity;

  return buf;
}

void byte_buffer_free(byte_buffer_t *buf) {
  if (!buf)
    return;

  free(buf->data);
  free(buf);
}

bool byte_buffer_append(byte_buffer_t *buf, const uint8_t *data, size_t len) {
  if (!buf)
    return false;
  if (len == 0)
    return true;
  if (!data)
    return false;

  if (buf->len + len > buf->capacity) {
    size_t new_capacity = buf->capacity;
    while (new_capacity < buf->len + len) {
      new_capacity *= 2;
    }

    uint8_t *new_data = safe_realloc(buf->data, new_capacity);
    if (!new_data)
      return false;

    buf->data = new_data;
    buf->capacity = new_capacity;
  }

  memcpy(buf->data + buf->len, data, len);
  buf->len += len;

  return true;
}

bool byte_buffer_append_byte(byte_buffer_t *buf, uint8_t byte) {
  return byte_buffer_append(buf, &byte, 1);
}

uint8_t *byte_buffer_get_data(byte_buffer_t *buf) {
  if (!buf)
    return NULL;
  return buf->data;
}

size_t byte_buffer_get_len(byte_buffer_t *buf) {
  if (!buf)
    return 0;
  return buf->len;
}

// Base58 encoding
static const char BASE58_ALPHABET[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

char *base58_encode(const uint8_t *data, size_t len) {
  if (!data || len == 0)
    return NULL;

  // Count leading zeros
  size_t leading_zeros = 0;
  for (size_t i = 0; i < len && data[i] == 0; i++) {
    leading_zeros++;
  }

  // Allocate enough space (log(256)/log(58) ≈ 1.37)
  size_t max_size = len * 138 / 100 + 1;
  uint8_t *encoded = safe_malloc(max_size);
  if (!encoded)
    return NULL;
  memset(encoded, 0, max_size);

  // Process the bytes
  size_t output_len = 0;
  for (size_t i = 0; i < len; i++) {
    uint32_t carry = data[i];
    size_t j = 0;

    for (size_t k = 0; k < output_len || carry; k++) {
      if (k < output_len) {
        carry += (uint32_t)(encoded[k]) << 8;
      }
      encoded[k] = carry % 58;
      carry /= 58;
      j = k + 1;
    }
    output_len = j;
  }

  // Convert to base58 characters and reverse
  size_t result_len = leading_zeros + output_len;
  char *result = safe_malloc(result_len + 1);
  if (!result) {
    free(encoded);
    return NULL;
  }

  // Add leading '1's for leading zeros
  for (size_t i = 0; i < leading_zeros; i++) {
    result[i] = '1';
  }

  // Reverse and convert to base58 alphabet
  for (size_t i = 0; i < output_len; i++) {
    result[leading_zeros + i] = BASE58_ALPHABET[encoded[output_len - 1 - i]];
  }
  result[result_len] = '\0';

  free(encoded);
  return result;
}

char *base58check_encode(const uint8_t *data, size_t len) {
  if (!data || len == 0)
    return NULL;

  // Calculate checksum (first 4 bytes of double SHA256)
  CRYAL_SHA256_CTX ctx;
  uint8_t hash1[32];
  uint8_t hash2[32];

  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, hash1);

  sha256_init(&ctx);
  sha256_update(&ctx, hash1, 32);
  sha256_final(&ctx, hash2);

  // Append checksum to data
  uint8_t *data_with_checksum = safe_malloc(len + 4);
  if (!data_with_checksum)
    return NULL;

  memcpy(data_with_checksum, data, len);
  memcpy(data_with_checksum + len, hash2, 4);

  // Encode with base58
  char *result = base58_encode(data_with_checksum, len + 4);
  free(data_with_checksum);

  return result;
}
