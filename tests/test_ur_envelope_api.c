/*
 * test_ur_envelope_api.c
 *
 * Exercises envelope-level API contracts used by integrators:
 *  - ur_decoder_received_parts_count(): counts unique recovered pure
 *    fragments, never exceeds the expected part count, ignores duplicate
 *    frames, and equals the expected count once decoding succeeds.
 *  - Single-part encoding: ur_encoder_is_complete() must become true after
 *    the single part has been emitted, and the emitted part must round-trip
 *    through the decoder.
 */

#include "../src/ur_decoder.h"
#include "../src/ur_encoder.h"
#include "test_harness.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CASES_DIR "tests/test_cases/bytes"

static bool test_file(const char *filepath) {
  printf("\n=== Testing file: %s ===\n", filepath);

  int fragment_count = 0;
  char **fragments = read_fragments_from_file(filepath, &fragment_count);
  if (!fragments || fragment_count == 0) {
    fprintf(stderr, "❌ No fragments found in file: %s\n", filepath);
    return false;
  }

  ur_decoder_t *decoder = ur_decoder_new();
  if (!decoder) {
    fprintf(stderr, "❌ Failed to create UR decoder\n");
    free_fragments(fragments, fragment_count);
    return false;
  }

  bool ok = true;
  if (ur_decoder_received_parts_count(decoder) != 0) {
    fprintf(stderr, "❌ Fresh decoder should report 0 received parts\n");
    ok = false;
  }

  size_t prev = 0;
  for (int i = 0; i < fragment_count && ok; i++) {
    ur_decoder_state_t state = ur_decoder_receive_part(decoder, fragments[i]);
    if (ur_decoder_state_is_error(state))
      continue;

    const size_t received = ur_decoder_received_parts_count(decoder);
    const size_t expected = ur_decoder_expected_part_count(decoder);

    if (received < prev) {
      fprintf(stderr, "❌ Frame %d: received count decreased %zu -> %zu\n", i,
              prev, received);
      ok = false;
    }
    if (received > expected) {
      fprintf(stderr, "❌ Frame %d: received %zu > expected %zu\n", i, received,
              expected);
      ok = false;
    }
    prev = received;

    // A duplicate of a frame already seen must not change the count
    if (ok &&
        !ur_decoder_state_is_error(
            ur_decoder_receive_part(decoder, fragments[i])) &&
        ur_decoder_received_parts_count(decoder) != received) {
      fprintf(stderr, "❌ Frame %d: duplicate changed received count\n", i);
      ok = false;
    }

    if (ur_decoder_state_is_terminal(state))
      break;
  }

  if (ok && ur_decoder_get_state(decoder) != UR_DECODER_OK) {
    fprintf(stderr, "❌ Decode did not succeed\n");
    ok = false;
  }
  if (ok && ur_decoder_received_parts_count(decoder) !=
                ur_decoder_expected_part_count(decoder)) {
    fprintf(stderr, "❌ On success received %zu != expected %zu\n",
            ur_decoder_received_parts_count(decoder),
            ur_decoder_expected_part_count(decoder));
    ok = false;
  }
  if (ok) {
    printf("✅ PASS - received-count invariants held (%zu parts)\n", prev);
  }

  ur_decoder_free(decoder);
  free_fragments(fragments, fragment_count);
  return ok;
}

// Single-part encoder: is_complete must flip true after emitting the part,
// and the part must decode back to the original payload.
static bool test_single_part_encoder(void) {
  printf("\n=== Testing single-part encoder completeness ===\n");

  const uint8_t cbor[] = {
      0x50, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}; // bytes(16)
  ur_encoder_t *encoder =
      ur_encoder_new("bytes", cbor, sizeof(cbor), 100, 0, 10);
  if (!encoder) {
    fprintf(stderr, "❌ Failed to create encoder\n");
    return false;
  }

  bool ok = true;
  if (!ur_encoder_is_single_part(encoder)) {
    fprintf(stderr, "❌ Expected a single-part encoder\n");
    ok = false;
  }
  if (ok && ur_encoder_is_complete(encoder)) {
    fprintf(stderr, "❌ Encoder complete before any part emitted\n");
    ok = false;
  }

  char *part = NULL;
  if (ok && !ur_encoder_next_part(encoder, &part)) {
    fprintf(stderr, "❌ next_part failed\n");
    ok = false;
  }
  if (ok && !ur_encoder_is_complete(encoder)) {
    fprintf(stderr, "❌ Encoder not complete after emitting single part\n");
    ok = false;
  }

  // Re-emitting the single part must keep working (animated QR loops)
  char *part2 = NULL;
  if (ok && (!ur_encoder_next_part(encoder, &part2) || strcmp(part, part2))) {
    fprintf(stderr, "❌ Re-emitted single part missing or different\n");
    ok = false;
  }

  if (ok) {
    ur_decoder_t *decoder = ur_decoder_new();
    if (!decoder || ur_decoder_receive_part(decoder, part) != UR_DECODER_OK) {
      fprintf(stderr, "❌ Single part did not decode\n");
      ok = false;
    } else {
      ur_result_t *result = ur_decoder_get_result(decoder);
      if (!result || result->cbor_len != sizeof(cbor) ||
          memcmp(result->cbor_data, cbor, sizeof(cbor)) ||
          strcmp(result->type, "bytes")) {
        fprintf(stderr, "❌ Single part round-trip mismatch\n");
        ok = false;
      }
    }
    ur_decoder_free(decoder);
  }

  free(part);
  free(part2);
  ur_encoder_free(encoder);

  if (ok) {
    printf("✅ PASS - single-part encoder completes and round-trips\n");
  }
  return ok;
}

int main(int argc, char *argv[]) {
  if (ur_decoder_received_parts_count(NULL) != 0) {
    fprintf(stderr, "❌ NULL decoder should report 0 received parts\n");
    return 1;
  }
  if (!test_single_part_encoder()) {
    return 1;
  }
  return run_test_suite(argc, argv, "UR Envelope API Test", TEST_CASES_DIR,
                        ".UR_fragments.txt", test_file);
}
