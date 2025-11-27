#include "utils.h"
#include "../sha256/sha256.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Memory management utilities removed - now using root utils.h implementation
// directly (safe_malloc, safe_realloc, safe_strdup, free)

// String utilities
char *urtypes_str_concat(const char *s1, const char *s2) {
  if (!s1 && !s2)
    return NULL;
  if (!s1)
    return safe_strdup(s2);
  if (!s2)
    return safe_strdup(s1);

  size_t len1 = strlen(s1);
  size_t len2 = strlen(s2);
  char *result = safe_malloc(len1 + len2 + 1);
  if (result) {
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2 + 1);
  }
  return result;
}

char *urtypes_str_concat_n(int count, ...) {
  if (count <= 0)
    return NULL;

  va_list args;
  size_t total_len = 0;

  // Calculate total length
  va_start(args, count);
  for (int i = 0; i < count; i++) {
    const char *str = va_arg(args, const char *);
    if (str) {
      total_len += strlen(str);
    }
  }
  va_end(args);

  // Allocate and concatenate
  char *result = safe_malloc(total_len + 1);
  if (!result)
    return NULL;

  char *ptr = result;
  va_start(args, count);
  for (int i = 0; i < count; i++) {
    const char *str = va_arg(args, const char *);
    if (str) {
      size_t len = strlen(str);
      memcpy(ptr, str, len);
      ptr += len;
    }
  }
  va_end(args);
  *ptr = '\0';

  return result;
}

char *urtypes_bytes_to_hex(const uint8_t *data, size_t len) {
  if (!data || len == 0)
    return NULL;

  char *hex = safe_malloc(len * 2 + 1);
  if (!hex)
    return NULL;

  for (size_t i = 0; i < len; i++) {
    sprintf(hex + i * 2, "%02x", data[i]);
  }
  hex[len * 2] = '\0';

  return hex;
}

uint8_t *urtypes_hex_to_bytes(const char *hex, size_t *out_len) {
  if (!hex || !out_len)
    return NULL;

  size_t hex_len = strlen(hex);
  if (hex_len % 2 != 0)
    return NULL;

  size_t byte_len = hex_len / 2;
  uint8_t *bytes = safe_malloc(byte_len);
  if (!bytes)
    return NULL;

  for (size_t i = 0; i < byte_len; i++) {
    char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    bytes[i] = (uint8_t)strtol(byte_str, NULL, 16);
  }

  *out_len = byte_len;
  return bytes;
}

// Array utilities
urtypes_array_t *urtypes_array_new(void) {
  urtypes_array_t *arr = safe_malloc(sizeof(urtypes_array_t));
  if (!arr)
    return NULL;

  arr->items = NULL;
  arr->count = 0;
  arr->capacity = 0;

  return arr;
}

void urtypes_array_free(urtypes_array_t *arr) {
  if (!arr)
    return;

  free(arr->items);
  free(arr);
}

bool urtypes_array_append(urtypes_array_t *arr, void *item) {
  if (!arr)
    return false;

  if (arr->count >= arr->capacity) {
    size_t new_capacity = arr->capacity == 0 ? 4 : arr->capacity * 2;
    void **new_items = safe_realloc(arr->items, new_capacity * sizeof(void *));
    if (!new_items)
      return false;

    arr->items = new_items;
    arr->capacity = new_capacity;
  }

  arr->items[arr->count++] = item;
  return true;
}

void *urtypes_array_get(urtypes_array_t *arr, size_t index) {
  if (!arr || index >= arr->count)
    return NULL;
  return arr->items[index];
}

size_t urtypes_array_count(urtypes_array_t *arr) {
  if (!arr)
    return 0;
  return arr->count;
}

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

byte_buffer_t *byte_buffer_new_from_data(const uint8_t *data, size_t len) {
  if (!data || len == 0)
    return NULL;

  byte_buffer_t *buf = byte_buffer_new_with_capacity(len);
  if (!buf)
    return NULL;

  memcpy(buf->data, data, len);
  buf->len = len;

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
  // Allow zero-length append (no-op) and NULL data if len is 0
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
