//
// cbor_lite.c
//
// Copyright © 2025 Krux Contributors
// Licensed under the "BSD-2-Clause Plus Patent License"
//
// Lightweight CBOR encoder/decoder implementation for UR encoding.
// Follows CBOR specification RFC 8949.
//
// This is an independent implementation written using
// foundation-ur-py as a reference for testing and validation.
//

#include "cbor_lite.h"
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

size_t cbor_encoded_unsigned_size(uint64_t value) {
  return 1 + get_byte_length(value);
}

// Decoder functions
void cbor_decoder_init(cbor_decoder_t *dec, const uint8_t *buffer,
                       size_t size) {
  if (dec) {
    dec->buffer = buffer;
    dec->size = size;
    dec->pos = 0;
  }
}

// Helper: Decode tag and additional info
static bool decode_tag_and_additional(cbor_decoder_t *dec, uint8_t *major_out,
                                      uint8_t *additional_out) {
  if (!dec || dec->pos >= dec->size)
    return false;

  uint8_t byte = dec->buffer[dec->pos++];
  *major_out = byte & CBOR_MAJOR_MASK;
  *additional_out = byte & CBOR_MINOR_MASK;
  return true;
}

// Helper: Decode value from additional info
static bool decode_value_from_additional(cbor_decoder_t *dec,
                                         uint8_t additional,
                                         uint64_t *value_out) {
  if (!dec || !value_out)
    return false;

  if (additional < CBOR_MINOR_LENGTH1) {
    *value_out = additional;
    return true;
  }

  uint64_t value = 0;

  if (additional == CBOR_MINOR_LENGTH1) {
    if (dec->pos + 1 > dec->size)
      return false;
    value = dec->buffer[dec->pos++];
  } else if (additional == CBOR_MINOR_LENGTH2) {
    if (dec->pos + 2 > dec->size)
      return false;
    value = ((uint64_t)dec->buffer[dec->pos] << 8) |
            (uint64_t)dec->buffer[dec->pos + 1];
    dec->pos += 2;
  } else if (additional == CBOR_MINOR_LENGTH4) {
    if (dec->pos + 4 > dec->size)
      return false;
    value = ((uint64_t)dec->buffer[dec->pos] << 24) |
            ((uint64_t)dec->buffer[dec->pos + 1] << 16) |
            ((uint64_t)dec->buffer[dec->pos + 2] << 8) |
            (uint64_t)dec->buffer[dec->pos + 3];
    dec->pos += 4;
  } else if (additional == CBOR_MINOR_LENGTH8) {
    if (dec->pos + 8 > dec->size)
      return false;
    value = ((uint64_t)dec->buffer[dec->pos] << 56) |
            ((uint64_t)dec->buffer[dec->pos + 1] << 48) |
            ((uint64_t)dec->buffer[dec->pos + 2] << 40) |
            ((uint64_t)dec->buffer[dec->pos + 3] << 32) |
            ((uint64_t)dec->buffer[dec->pos + 4] << 24) |
            ((uint64_t)dec->buffer[dec->pos + 5] << 16) |
            ((uint64_t)dec->buffer[dec->pos + 6] << 8) |
            (uint64_t)dec->buffer[dec->pos + 7];
    dec->pos += 8;
  } else {
    return false;
  }

  *value_out = value;
  return true;
}

bool cbor_decode_unsigned(cbor_decoder_t *dec, uint64_t *value_out) {
  if (!dec || !value_out)
    return false;

  uint8_t major, additional;
  if (!decode_tag_and_additional(dec, &major, &additional))
    return false;

  if (major != CBOR_MAJOR_UNSIGNED)
    return false;

  return decode_value_from_additional(dec, additional, value_out);
}

bool cbor_decode_array_start(cbor_decoder_t *dec, size_t *count_out) {
  if (!dec || !count_out)
    return false;

  uint8_t major, additional;
  if (!decode_tag_and_additional(dec, &major, &additional))
    return false;

  if (major != CBOR_MAJOR_ARRAY)
    return false;

  uint64_t count;
  if (!decode_value_from_additional(dec, additional, &count))
    return false;

  *count_out = (size_t)count;
  return true;
}

bool cbor_decode_bytes(cbor_decoder_t *dec, const uint8_t **data_out,
                       size_t *len_out) {
  if (!dec || !data_out || !len_out)
    return false;

  uint8_t major, additional;
  if (!decode_tag_and_additional(dec, &major, &additional))
    return false;

  if (major != CBOR_MAJOR_BYTES)
    return false;

  uint64_t len;
  if (!decode_value_from_additional(dec, additional, &len))
    return false;

  if (dec->pos + len > dec->size)
    return false;

  *data_out = dec->buffer + dec->pos;
  *len_out = (size_t)len;
  dec->pos += len;

  return true;
}
