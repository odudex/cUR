/*
 * test_ur_gaussian.c
 *
 * Covers the mixed-equation (Gaussian elimination) side of the fountain
 * decoder.
 *
 * The checked-in fragment files do reach the equation table incidentally, but
 * only in the one shape they happen to have: a complete stream replayed in
 * order. Nothing pins down the orderings a camera actually produces, where
 * dropped frames mean fountain parts (seq_num > seq_len) mixing several
 * fragments arrive long before every pure one has been seen.
 *
 * These cases fix those orderings: a message decoded from fountain parts
 * alone, one where every mixed part precedes the pure ones, and a lossy
 * interleave. Each forces reduce_mixed_by() to promote recovered fragments
 * onto the work queue in batches - the path where a dropped fragment would
 * degrade decoding silently rather than fail it.
 */

#include "../src/ur.h"
#include "../src/ur_decoder.h"
#include "../src/ur_encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int asserts;
static int failures;

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

#define MAX_FRAGMENT_LEN 40
#define PAYLOAD_LEN 500
#define MAX_PARTS 400
#define SUBSET_ONLY_FOUNTAIN_PARTS 17

// Deterministic, non-repeating payload so a mis-assembled message cannot
// coincidentally compare equal.
static void fill_payload(uint8_t *buf, size_t len) {
  uint32_t x = 0x12345678u;
  for (size_t i = 0; i < len; i++) {
    x = x * 1103515245u + 12345u;
    buf[i] = (uint8_t)(x >> 16);
  }
}

// Generate `count` UR parts. Caller frees each string and the array.
static char **encode_parts(const uint8_t *payload, size_t payload_len,
                           size_t count, size_t *seq_len_out) {
  ur_encoder_t *enc =
      ur_encoder_new("bytes", payload, payload_len, MAX_FRAGMENT_LEN, 0, 10);
  if (!enc)
    return NULL;

  *seq_len_out = ur_encoder_seq_len(enc);

  char **parts = calloc(count, sizeof(char *));
  if (!parts) {
    ur_encoder_free(enc);
    return NULL;
  }

  for (size_t i = 0; i < count; i++) {
    if (!ur_encoder_next_part(enc, &parts[i])) {
      for (size_t j = 0; j < i; j++)
        free(parts[j]);
      free(parts);
      ur_encoder_free(enc);
      return NULL;
    }
  }

  ur_encoder_free(enc);
  return parts;
}

static void free_parts(char **parts, size_t count) {
  if (!parts)
    return;
  for (size_t i = 0; i < count; i++)
    free(parts[i]);
  free(parts);
}

// Feed parts[first..last) and report whether the message completed intact.
static bool decode_range(char **parts, size_t first, size_t last,
                         const uint8_t *expect, size_t expect_len,
                         size_t *parts_used) {
  ur_decoder_t *dec = ur_decoder_new();
  if (!dec)
    return false;

  bool complete = false;
  size_t used = 0;

  for (size_t i = first; i < last; i++) {
    ur_decoder_state_t s = ur_decoder_receive_part(dec, parts[i]);
    used++;
    if (s == UR_DECODER_OK) {
      complete = true;
      break;
    }
    if (ur_decoder_state_is_error(s)) {
      fprintf(stderr, "  unexpected error state %d at part %zu\n", (int)s, i);
      ur_decoder_free(dec);
      return false;
    }
  }

  bool ok = false;
  if (complete) {
    ur_result_t *r = ur_decoder_get_result(dec);
    ok = r && r->cbor_len == expect_len &&
         memcmp(r->cbor_data, expect, expect_len) == 0;
    if (!ok)
      fprintf(stderr, "  completed but payload did not match\n");
  }

  if (parts_used)
    *parts_used = used;
  ur_decoder_free(dec);
  return ok;
}

int main(void) {
  printf("=== UR Gaussian-Elimination Tests ===\n");

  uint8_t payload[PAYLOAD_LEN];
  fill_payload(payload, sizeof(payload));

  size_t seq_len = 0;
  char **parts = encode_parts(payload, sizeof(payload), MAX_PARTS, &seq_len);
  ASSERT(parts != NULL, "encoded the test stream");
  if (!parts)
    return 1;

  ASSERT(seq_len > 1, "payload splits into multiple fragments");
  printf("  seq_len = %zu, generated %d parts\n", seq_len, MAX_PARTS);

  // Fountain parts only. Every part mixes several fragments, so the message
  // can only come out of the equation table - this is the path the checked-in
  // fragment files never touch.
  size_t used_fountain = 0;
  ASSERT(decode_range(parts, seq_len, MAX_PARTS, payload, sizeof(payload),
                      &used_fountain),
         "decodes from fountain parts alone");
  printf("  [parts consumed: %zu]\n", used_fountain);
  ASSERT(used_fountain <= SUBSET_ONLY_FOUNTAIN_PARTS,
         "fountain-only decode does not regress its fragment count");
#ifdef ENABLE_CROSS_REDUCTION
  ASSERT(used_fountain < SUBSET_ONLY_FOUNTAIN_PARTS,
         "cross reduction needs fewer fragments than subset-only reduction");
#endif

  // Mixed parts first, then the pure ones. Equations pile up unresolved, and
  // then a single pure part resolves a batch of them at once - the case where
  // reduce_mixed_by() promotes several recovered fragments to the work queue
  // in one call, and where losing one would stall the decode.
  {
    size_t batch = seq_len * 2;
    if (batch > MAX_PARTS - seq_len)
      batch = MAX_PARTS - seq_len;

    ur_decoder_t *dec = ur_decoder_new();
    ASSERT(dec != NULL, "created decoder for the mixed-first stream");
    if (dec) {
      bool complete = false;
      for (size_t i = 0; i < batch && !complete; i++) {
        if (ur_decoder_receive_part(dec, parts[seq_len + i]) == UR_DECODER_OK)
          complete = true;
      }
      for (size_t i = 0; i < seq_len && !complete; i++) {
        if (ur_decoder_receive_part(dec, parts[i]) == UR_DECODER_OK)
          complete = true;
      }

      ur_result_t *r = complete ? ur_decoder_get_result(dec) : NULL;
      ASSERT(r && r->cbor_len == sizeof(payload) &&
                 memcmp(r->cbor_data, payload, sizeof(payload)) == 0,
             "decodes with every mixed part arriving before the pure ones");
      ur_decoder_free(dec);
    }
  }

  // Interleaved, which is what a camera actually sees: some pure parts, a run
  // of fountain parts, then the rest.
  {
    ur_decoder_t *dec = ur_decoder_new();
    ASSERT(dec != NULL, "created decoder for the interleaved stream");
    if (dec) {
      bool complete = false;
      for (size_t i = 0; i < MAX_PARTS && !complete; i++) {
        // Take every third part, so roughly two thirds of the pure ones are
        // missing when the fountain parts start arriving.
        if (i % 3)
          continue;
        if (ur_decoder_receive_part(dec, parts[i]) == UR_DECODER_OK)
          complete = true;
      }

      ur_result_t *r = complete ? ur_decoder_get_result(dec) : NULL;
      ASSERT(r && r->cbor_len == sizeof(payload) &&
                 memcmp(r->cbor_data, payload, sizeof(payload)) == 0,
             "decodes a lossy interleaved stream");
      ur_decoder_free(dec);
    }
  }

  free_parts(parts, MAX_PARTS);

  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/%d\n", asserts - failures, asserts);
  return failures == 0 ? 0 : 1;
}
