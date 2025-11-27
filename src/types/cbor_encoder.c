#include "cbor_encoder.h"
#include <string.h>

// CBOR major types
#define CBOR_MAJOR_UNSIGNED_INT 0
#define CBOR_MAJOR_NEGATIVE_INT 1
#define CBOR_MAJOR_BYTES 2
#define CBOR_MAJOR_STRING 3
#define CBOR_MAJOR_ARRAY 4
#define CBOR_MAJOR_MAP 5
#define CBOR_MAJOR_TAG 6
#define CBOR_MAJOR_SIMPLE 7

// Helper functions for encoding
static bool encode_initial_byte(byte_buffer_t *buf, uint8_t major_type,
                                uint64_t value) {
  if (value < 24) {
    uint8_t byte = (major_type << 5) | (uint8_t)value;
    return byte_buffer_append_byte(buf, byte);
  } else if (value <= 0xFF) {
    uint8_t bytes[2] = {(major_type << 5) | 24, (uint8_t)value};
    return byte_buffer_append(buf, bytes, 2);
  } else if (value <= 0xFFFF) {
    uint8_t bytes[3] = {(major_type << 5) | 25, (uint8_t)(value >> 8),
                        (uint8_t)value};
    return byte_buffer_append(buf, bytes, 3);
  } else if (value <= 0xFFFFFFFF) {
    uint8_t bytes[5] = {(major_type << 5) | 26, (uint8_t)(value >> 24),
                        (uint8_t)(value >> 16), (uint8_t)(value >> 8),
                        (uint8_t)value};
    return byte_buffer_append(buf, bytes, 5);
  } else {
    uint8_t bytes[9] = {
        (major_type << 5) | 27, (uint8_t)(value >> 56), (uint8_t)(value >> 48),
        (uint8_t)(value >> 40), (uint8_t)(value >> 32), (uint8_t)(value >> 24),
        (uint8_t)(value >> 16), (uint8_t)(value >> 8),  (uint8_t)value};
    return byte_buffer_append(buf, bytes, 9);
  }
}

static bool encode_unsigned_int(byte_buffer_t *buf, uint64_t value) {
  return encode_initial_byte(buf, CBOR_MAJOR_UNSIGNED_INT, value);
}

static bool encode_negative_int(byte_buffer_t *buf, int64_t value) {
  // CBOR negative integers are encoded as -1 - value
  uint64_t encoded_value = (uint64_t)((-1) - value);
  return encode_initial_byte(buf, CBOR_MAJOR_NEGATIVE_INT, encoded_value);
}

static bool encode_bytes(byte_buffer_t *buf, const uint8_t *data, size_t len) {
  if (!encode_initial_byte(buf, CBOR_MAJOR_BYTES, len))
    return false;
  return byte_buffer_append(buf, data, len);
}

static bool encode_string(byte_buffer_t *buf, const char *str) {
  size_t len = strlen(str);
  if (!encode_initial_byte(buf, CBOR_MAJOR_STRING, len))
    return false;
  return byte_buffer_append(buf, (const uint8_t *)str, len);
}

static bool encode_value(byte_buffer_t *buf, cbor_value_t *value);

static bool encode_array(byte_buffer_t *buf, cbor_value_t *value) {
  size_t count = cbor_value_get_array_size(value);
  if (!encode_initial_byte(buf, CBOR_MAJOR_ARRAY, count))
    return false;

  for (size_t i = 0; i < count; i++) {
    cbor_value_t *item = cbor_value_get_array_item(value, i);
    if (!encode_value(buf, item))
      return false;
  }

  return true;
}

static bool encode_map(byte_buffer_t *buf, cbor_value_t *value) {
  size_t count = cbor_value_get_map_size(value);
  if (!encode_initial_byte(buf, CBOR_MAJOR_MAP, count))
    return false;

  if (value->type != CBOR_TYPE_MAP)
    return false;

  for (size_t i = 0; i < count; i++) {
    if (!encode_value(buf, value->value.map_val.keys[i]))
      return false;
    if (!encode_value(buf, value->value.map_val.values[i]))
      return false;
  }

  return true;
}

static bool encode_tag(byte_buffer_t *buf, cbor_value_t *value) {
  uint64_t tag = cbor_value_get_tag(value);
  if (!encode_initial_byte(buf, CBOR_MAJOR_TAG, tag))
    return false;

  cbor_value_t *content = cbor_value_get_tag_content(value);
  return encode_value(buf, content);
}

static bool encode_simple(byte_buffer_t *buf, cbor_value_t *value) {
  switch (value->type) {
  case CBOR_TYPE_BOOL: {
    uint8_t byte = (CBOR_MAJOR_SIMPLE << 5) | (value->value.bool_val ? 21 : 20);
    return byte_buffer_append_byte(buf, byte);
  }
  case CBOR_TYPE_NULL: {
    uint8_t byte = (CBOR_MAJOR_SIMPLE << 5) | 22;
    return byte_buffer_append_byte(buf, byte);
  }
  case CBOR_TYPE_UNDEFINED: {
    uint8_t byte = (CBOR_MAJOR_SIMPLE << 5) | 23;
    return byte_buffer_append_byte(buf, byte);
  }
  case CBOR_TYPE_FLOAT: {
    // Encode as double precision (64-bit)
    uint8_t bytes[9];
    bytes[0] = (CBOR_MAJOR_SIMPLE << 5) | 27;

    // Convert double to bytes (big-endian)
    uint64_t float_bits;
    memcpy(&float_bits, &value->value.float_val, sizeof(double));

    bytes[1] = (uint8_t)(float_bits >> 56);
    bytes[2] = (uint8_t)(float_bits >> 48);
    bytes[3] = (uint8_t)(float_bits >> 40);
    bytes[4] = (uint8_t)(float_bits >> 32);
    bytes[5] = (uint8_t)(float_bits >> 24);
    bytes[6] = (uint8_t)(float_bits >> 16);
    bytes[7] = (uint8_t)(float_bits >> 8);
    bytes[8] = (uint8_t)float_bits;

    return byte_buffer_append(buf, bytes, 9);
  }
  default:
    return false;
  }
}

static bool encode_value(byte_buffer_t *buf, cbor_value_t *value) {
  if (!value)
    return false;

  switch (value->type) {
  case CBOR_TYPE_UNSIGNED_INT:
    return encode_unsigned_int(buf, value->value.uint_val);
  case CBOR_TYPE_NEGATIVE_INT:
    return encode_negative_int(buf, value->value.int_val);
  case CBOR_TYPE_BYTES: {
    size_t len;
    const uint8_t *data = cbor_value_get_bytes(value, &len);
    return encode_bytes(buf, data, len);
  }
  case CBOR_TYPE_STRING:
    return encode_string(buf, value->value.string_val);
  case CBOR_TYPE_ARRAY:
    return encode_array(buf, value);
  case CBOR_TYPE_MAP:
    return encode_map(buf, value);
  case CBOR_TYPE_TAG:
    return encode_tag(buf, value);
  case CBOR_TYPE_BOOL:
  case CBOR_TYPE_NULL:
  case CBOR_TYPE_UNDEFINED:
  case CBOR_TYPE_FLOAT:
    return encode_simple(buf, value);
  default:
    return false;
  }
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
