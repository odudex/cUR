/*
 * test_ur_alloc.c
 *
 * Allocation behaviour, observed by wrapping malloc() at link time
 * (-Wl,--wrap=malloc, GNU ld only; the Makefile builds this test on Linux).
 */

#include "../src/types/bip39.h"
#include "../src/types/bytes_type.h"
#include "../src/ur_decoder.h"
#include "../src/ur_encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int asserts = 0;
static int failures = 0;

#define ASSERT(cond, msg)                                                      \
  do {                                                                         \
    asserts++;                                                                 \
    if (!(cond)) {                                                             \
      fprintf(stderr, "  FAIL @%d: %s\n", __LINE__, msg);                      \
      failures++;                                                              \
    } else {                                                                   \
      printf("  PASS %s\n", msg);                                              \
    }                                                                          \
  } while (0)

void *__real_malloc(size_t size);

static size_t fail_size; // fail the next malloc() of exactly this size
static size_t largest;   // largest malloc() since the last reset

void *__wrap_malloc(size_t size) {
  if (fail_size && size == fail_size) {
    fail_size = 0;
    return NULL;
  }
  if (size > largest)
    largest = size;
  return __real_malloc(size);
}

static void test_reassembly_retry(void) {
  printf("\n=== reassembly_retry ===\n");

  // No other allocation made while decoding this message has this size, so
  // failing it hits exactly the reassembly buffer.
  enum { MESSAGE_LEN = 1237 };
  uint8_t message[MESSAGE_LEN];
  for (size_t i = 0; i < sizeof message; i++)
    message[i] = (uint8_t)(i * 31 + 7);

  ur_encoder_t *enc =
      ur_encoder_new("bytes", message, sizeof message, 100, 0, 10);
  ur_decoder_t *dec = ur_decoder_new();
  ASSERT(enc && dec, "created encoder and decoder");
  if (!enc || !dec) {
    ur_encoder_free(enc);
    ur_decoder_free(dec);
    return;
  }

  size_t seq_len = ur_encoder_seq_len(enc);
  ur_decoder_state_t state = UR_DECODER_PROCESSING;
  for (size_t i = 0; i <= seq_len; i++) {
    char *part = NULL;
    if (!ur_encoder_next_part(enc, &part))
      break;
    if (i == seq_len - 1)
      fail_size = MESSAGE_LEN;
    state = ur_decoder_receive_part(dec, part);
    free(part);
    if (i == seq_len - 1) {
      ASSERT(fail_size == 0, "the reassembly allocation was failed");
      ASSERT(state == UR_DECODER_ERROR_MEMORY,
             "failed reassembly reports a transient MEMORY error");
    }
  }

  ur_result_t *result = ur_decoder_get_result(dec);
  ASSERT(state == UR_DECODER_OK && result && result->cbor_len == MESSAGE_LEN &&
             memcmp(result->cbor_data, message, MESSAGE_LEN) == 0,
         "the next part completes the message");

  ur_decoder_free(dec);
  ur_encoder_free(enc);
}

// A string head declaring more bytes than the input holds must be rejected
// before anything that size is allocated.
static void test_cbor_length_checked_before_alloc(void) {
  printf("\n=== cbor_length_checked_before_alloc ===\n");

  const uint8_t bytes_head[] = {0x5a, 0x00, 0x04, 0x00, 0x00}; // 256 KiB
  largest = 0;
  bytes_data_t *bytes = bytes_from_cbor(bytes_head, sizeof bytes_head);
  ASSERT(!bytes, "truncated byte string is rejected");
  ASSERT(largest < 1024, "  -> without allocating its declared length");
  bytes_free(bytes);

  // {1: [<text of 256 KiB>]}, the text itself missing
  const uint8_t text_head[] = {0xa1, 0x01, 0x81, 0x7a, 0x00, 0x04, 0x00, 0x00};
  largest = 0;
  bip39_data_t *bip39 = bip39_from_cbor(text_head, sizeof text_head);
  ASSERT(!bip39, "truncated text string is rejected");
  ASSERT(largest < 1024, "  -> without allocating its declared length");
  bip39_free(bip39);
}

int main(void) {
  printf("=== UR Allocation Tests ===\n");
  test_reassembly_retry();
  test_cbor_length_checked_before_alloc();
  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/%d\n", asserts - failures, asserts);
  return failures == 0 ? 0 : 1;
}
