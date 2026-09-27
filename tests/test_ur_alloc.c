/*
 * test_ur_alloc.c
 *
 * Allocation behaviour, observed by wrapping malloc() at link time
 * (-Wl,--wrap=malloc, GNU ld only; the Makefile builds this test on Linux).
 */

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

void *__wrap_malloc(size_t size) {
  if (fail_size && size == fail_size) {
    fail_size = 0;
    return NULL;
  }
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

int main(void) {
  printf("=== UR Allocation Tests ===\n");
  test_reassembly_retry();
  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/%d\n", asserts - failures, asserts);
  return failures == 0 ? 0 : 1;
}
