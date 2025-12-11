#include "cbor_encoder.h"
#include <string.h>

// CBOR major type for bytes
#define CBOR_MAJOR_BYTES 2

// Encode length prefix for bytes
static bool encode_bytes_length(byte_buffer_t *buf, size_t len) {
  if (len < 24) {
    uint8_t byte = (CBOR_MAJOR_BYTES << 5) | (uint8_t)len;
    return byte_buffer_append_byte(buf, byte);
  } else if (len <= 0xFF) {
    uint8_t bytes[2] = {(CBOR_MAJOR_BYTES << 5) | 24, (uint8_t)len};
    return byte_buffer_append(buf, bytes, 2);
  } else if (len <= 0xFFFF) {
    uint8_t bytes[3] = {(CBOR_MAJOR_BYTES << 5) | 25, (uint8_t)(len >> 8),
                        (uint8_t)len};
    return byte_buffer_append(buf, bytes, 3);
  } else if (len <= 0xFFFFFFFF) {
    uint8_t bytes[5] = {(CBOR_MAJOR_BYTES << 5) | 26, (uint8_t)(len >> 24),
                        (uint8_t)(len >> 16), (uint8_t)(len >> 8),
                        (uint8_t)len};
    return byte_buffer_append(buf, bytes, 5);
  } else {
    uint64_t len64 = (uint64_t)len;
    uint8_t bytes[9] = {(CBOR_MAJOR_BYTES << 5) | 27,
                        (uint8_t)(len64 >> 56),
                        (uint8_t)(len64 >> 48),
                        (uint8_t)(len64 >> 40),
                        (uint8_t)(len64 >> 32),
                        (uint8_t)(len64 >> 24),
                        (uint8_t)(len64 >> 16),
                        (uint8_t)(len64 >> 8),
                        (uint8_t)len64};
    return byte_buffer_append(buf, bytes, 9);
  }
}

// Encode bytes value
static bool encode_bytes(byte_buffer_t *buf, const uint8_t *data, size_t len) {
  if (!encode_bytes_length(buf, len))
    return false;
  return byte_buffer_append(buf, data, len);
}

// Encode a CBOR value (only bytes type supported)
static bool encode_value(byte_buffer_t *buf, cbor_value_t *value) {
  if (!value)
    return false;

  if (value->type != CBOR_TYPE_BYTES)
    return false;

  size_t len;
  const uint8_t *data = cbor_value_get_bytes(value, &len);
  return encode_bytes(buf, data, len);
}

// Create and destroy encoder
urtypes_cbor_encoder_t *urtypes_cbor_encoder_new(void) {
  urtypes_cbor_encoder_t *encoder = safe_malloc(sizeof(urtypes_cbor_encoder_t));
  if (!encoder)
    return NULL;

  encoder->buffer = byte_buffer_new();
  if (!encoder->buffer) {
    free(encoder);
    return NULL;
  }

  return encoder;
}

void urtypes_cbor_encoder_free(urtypes_cbor_encoder_t *encoder) {
  if (!encoder)
    return;

  byte_buffer_free(encoder->buffer);
  free(encoder);
}

// Encode CBOR value
bool urtypes_cbor_encoder_encode(urtypes_cbor_encoder_t *encoder,
                                 cbor_value_t *value) {
  if (!encoder || !value)
    return false;

  return encode_value(encoder->buffer, value);
}

// Get encoded data
uint8_t *urtypes_cbor_encoder_get_data(urtypes_cbor_encoder_t *encoder,
                                       size_t *out_len) {
  if (!encoder || !out_len)
    return NULL;

  *out_len = byte_buffer_get_len(encoder->buffer);

  // Return a copy of the data
  uint8_t *data = safe_malloc(*out_len);
  if (!data)
    return NULL;

  memcpy(data, byte_buffer_get_data(encoder->buffer), *out_len);
  return data;
}

// Convenience function to encode a value to bytes
uint8_t *cbor_encode(cbor_value_t *value, size_t *out_len) {
  if (!value || !out_len)
    return NULL;

  urtypes_cbor_encoder_t *encoder = urtypes_cbor_encoder_new();
  if (!encoder)
    return NULL;

  if (!urtypes_cbor_encoder_encode(encoder, value)) {
    urtypes_cbor_encoder_free(encoder);
    return NULL;
  }

  uint8_t *data = urtypes_cbor_encoder_get_data(encoder, out_len);
  urtypes_cbor_encoder_free(encoder);

  return data;
}
