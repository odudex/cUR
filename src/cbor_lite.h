#ifndef CBOR_LITE_H
#define CBOR_LITE_H

/*
 * cbor_lite.h - Lightweight CBOR encoder/decoder
 *
 * Minimal CBOR implementation for UR encoding/decoding
 * Based on cbor-lite from Isode Limited (MIT License)
 * Adapted from Python implementation in cbor_lite.py
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CBOR major types
#define CBOR_MAJOR_UNSIGNED 0
#define CBOR_MAJOR_NEGATIVE (1 << 5)
#define CBOR_MAJOR_BYTES (2 << 5)
#define CBOR_MAJOR_TEXT (3 << 5)
#define CBOR_MAJOR_ARRAY (4 << 5)
#define CBOR_MAJOR_MAP (5 << 5)
#define CBOR_MAJOR_SEMANTIC (6 << 5)
#define CBOR_MAJOR_SIMPLE (7 << 5)
#define CBOR_MAJOR_MASK 0xe0

// CBOR additional info values
#define CBOR_MINOR_LENGTH1 24
#define CBOR_MINOR_LENGTH2 25
#define CBOR_MINOR_LENGTH4 26
#define CBOR_MINOR_LENGTH8 27
#define CBOR_MINOR_MASK 0x1f

// Simple encoder for UR use cases
typedef struct {
  uint8_t *buffer;
  size_t capacity;
  size_t size;
} cbor_encoder_t;

// Simple decoder for UR use cases
typedef struct {
  const uint8_t *buffer;
  size_t size;
  size_t pos;
} cbor_decoder_t;

// Encoder functions
bool cbor_encoder_init(cbor_encoder_t *enc, size_t initial_capacity);
void cbor_encoder_free(cbor_encoder_t *enc);
uint8_t *cbor_encoder_get_buffer(cbor_encoder_t *enc, size_t *size_out);

bool cbor_encode_unsigned(cbor_encoder_t *enc, uint64_t value);
bool cbor_encode_array_start(cbor_encoder_t *enc, size_t count);
bool cbor_encode_bytes(cbor_encoder_t *enc, const uint8_t *data, size_t len);

// Decoder functions
void cbor_decoder_init(cbor_decoder_t *dec, const uint8_t *buffer, size_t size);
bool cbor_decode_unsigned(cbor_decoder_t *dec, uint64_t *value_out);
bool cbor_decode_array_start(cbor_decoder_t *dec, size_t *count_out);
bool cbor_decode_bytes(cbor_decoder_t *dec, const uint8_t **data_out,
                       size_t *len_out);

// Helper to get size of encoded unsigned value
size_t cbor_encoded_unsigned_size(uint64_t value);

#endif // CBOR_LITE_H
