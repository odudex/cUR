//
// cbor_encoder_lite.c
//
// Copyright © 2025 Krux Contributors
// Licensed under the "BSD-2-Clause Plus Patent License"
//
// Lightweight CBOR encoder implementation for UR fountain encoding.
// Follows CBOR specification RFC 8949.
//
// This is an independent implementation written using
// foundation-ur-py as a reference for testing and validation.
//
// Note: Decoder functionality removed - all decoding done by
// types/cbor_decoder.c
//

#include "cbor_encoder_lite.h"
#include <stdlib.h>
#include <string.h>

// Helper: Get byte length needed for a value
static size_t get_byte_length(uint64_t value) {
  if (value < 24)
    return 0;
  if (value <= 0xFF)
    return 1;
  if (value <= 0xFFFF)
    return 2;
  if (value <= 0xFFFFFFFF)
    return 4;
  return 8;
}

// Helper: Ensure encoder has enough capacity
static bool ensure_capacity(cbor_encoder_t *enc, size_t additional) {
  if (!enc)
    return false;

  size_t required = enc->size + additional;
  if (required <= enc->capacity)
    return true;

  // Grow by 1.5x or to required size, whichever is larger
  size_t new_capacity = enc->capacity * 3 / 2;
  if (new_capacity < required)
    new_capacity = required;

  uint8_t *new_buffer = (uint8_t *)realloc(enc->buffer, new_capacity);
  if (!new_buffer)
    return false;

  enc->buffer = new_buffer;
  enc->capacity = new_capacity;
  return true;
}

// Helper: Append byte to encoder
static bool append_byte(cbor_encoder_t *enc, uint8_t byte) {
  if (!ensure_capacity(enc, 1))
    return false;
  enc->buffer[enc->size++] = byte;
  return true;
}

// Helper: Encode tag and value
static bool encode_tag_and_value(cbor_encoder_t *enc, uint8_t major_type,
                                 uint64_t value) {
  size_t length = get_byte_length(value);

  if (length == 0) {
    // Value fits in additional info
    return append_byte(enc, major_type + (uint8_t)value);
  }

  if (!ensure_capacity(enc, 1 + length))
    return false;

  if (length == 1) {
    append_byte(enc, major_type + CBOR_MINOR_LENGTH1);
    append_byte(enc, value & 0xFF);
  } else if (length == 2) {
    append_byte(enc, major_type + CBOR_MINOR_LENGTH2);
    append_byte(enc, (value >> 8) & 0xFF);
    append_byte(enc, value & 0xFF);
  } else if (length == 4) {
    append_byte(enc, major_type + CBOR_MINOR_LENGTH4);
    append_byte(enc, (value >> 24) & 0xFF);
    append_byte(enc, (value >> 16) & 0xFF);
    append_byte(enc, (value >> 8) & 0xFF);
    append_byte(enc, value & 0xFF);
  } else { // length == 8
    append_byte(enc, major_type + CBOR_MINOR_LENGTH8);
    append_byte(enc, (value >> 56) & 0xFF);
    append_byte(enc, (value >> 48) & 0xFF);
    append_byte(enc, (value >> 40) & 0xFF);
    append_byte(enc, (value >> 32) & 0xFF);
    append_byte(enc, (value >> 24) & 0xFF);
    append_byte(enc, (value >> 16) & 0xFF);
    append_byte(enc, (value >> 8) & 0xFF);
    append_byte(enc, value & 0xFF);
  }

  return true;
}

// Encoder functions
bool cbor_encoder_init(cbor_encoder_t *enc, size_t initial_capacity) {
  if (!enc)
    return false;

  enc->buffer = (uint8_t *)malloc(initial_capacity);
  if (!enc->buffer)
    return false;

  enc->capacity = initial_capacity;
  enc->size = 0;
  return true;
}

void cbor_encoder_free(cbor_encoder_t *enc) {
  if (enc) {
    free(enc->buffer);
    enc->buffer = NULL;
    enc->capacity = 0;
    enc->size = 0;
  }
}

uint8_t *cbor_encoder_get_buffer(cbor_encoder_t *enc, size_t *size_out) {
  if (!enc || !size_out)
    return NULL;

  *size_out = enc->size;

  // Allocate new buffer and copy data
  uint8_t *result = (uint8_t *)malloc(enc->size);
  if (!result)
    return NULL;

  memcpy(result, enc->buffer, enc->size);
  return result;
}

bool cbor_encode_unsigned(cbor_encoder_t *enc, uint64_t value) {
  return encode_tag_and_value(enc, CBOR_MAJOR_UNSIGNED, value);
}

bool cbor_encode_array_start(cbor_encoder_t *enc, size_t count) {
  return encode_tag_and_value(enc, CBOR_MAJOR_ARRAY, count);
}

bool cbor_encode_bytes(cbor_encoder_t *enc, const uint8_t *data, size_t len) {
  if (!enc || !data)
    return false;

  if (!encode_tag_and_value(enc, CBOR_MAJOR_BYTES, len))
    return false;

  if (!ensure_capacity(enc, len))
    return false;

  memcpy(enc->buffer + enc->size, data, len);
  enc->size += len;
  return true;
}
