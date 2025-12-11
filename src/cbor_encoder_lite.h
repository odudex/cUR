#ifndef CBOR_ENCODER_LITE_H
#define CBOR_ENCODER_LITE_H

/*
 * cbor_encoder_lite.h - Lightweight CBOR encoder
 *
 * Minimal CBOR encoder implementation for UR fountain encoding.
 * Based on cbor-lite from Isode Limited (MIT License).
 * Adapted from Python implementation in cbor_lite.py.
 *
 * Note: This is encoder-only. All decoding is done by types/cbor_decoder.c
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CBOR major types
#define CBOR_MAJOR_UNSIGNED 0
#define CBOR_MAJOR_BYTES (2 << 5)
#define CBOR_MAJOR_ARRAY (4 << 5)
#define CBOR_MAJOR_MASK 0xe0

// CBOR additional info values
#define CBOR_MINOR_LENGTH1 24
#define CBOR_MINOR_LENGTH2 25
#define CBOR_MINOR_LENGTH4 26
#define CBOR_MINOR_LENGTH8 27
#define CBOR_MINOR_MASK 0x1f

// Simple encoder for UR fountain encoding use cases
typedef struct {
  uint8_t *buffer;
  size_t capacity;
  size_t size;
} cbor_encoder_t;

// Encoder functions
bool cbor_encoder_init(cbor_encoder_t *enc, size_t initial_capacity);
void cbor_encoder_free(cbor_encoder_t *enc);
uint8_t *cbor_encoder_get_buffer(cbor_encoder_t *enc, size_t *size_out);

bool cbor_encode_unsigned(cbor_encoder_t *enc, uint64_t value);
bool cbor_encode_array_start(cbor_encoder_t *enc, size_t count);
bool cbor_encode_bytes(cbor_encoder_t *enc, const uint8_t *data, size_t len);

#endif // CBOR_ENCODER_LITE_H
