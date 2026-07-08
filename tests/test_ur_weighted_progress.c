/*
 * test_ur_weighted_progress.c
 *
 * Exercises ur_decoder_estimated_percent_complete_weighted() against real
 * multi-fragment streams and asserts its invariants after every frame:
 * the value stays in [0, 0.99] while the decode is incomplete, never
 * decreases, and reads exactly 1.0 once the decoder completes.
 */

#include "../src/ur_decoder.h"
#include "test_harness.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>

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
  if (ur_decoder_estimated_percent_complete_weighted(decoder) != 0.0) {
    fprintf(stderr, "❌ Fresh decoder should report 0.0\n");
    ok = false;
  }

  double prev = 0.0;
  int parts_used = 0;
  for (int i = 0; i < fragment_count && ok; i++) {
    ur_decoder_state_t state = ur_decoder_receive_part(decoder, fragments[i]);
    if (ur_decoder_state_is_error(state))
      continue;
    parts_used++;

    double weighted = ur_decoder_estimated_percent_complete_weighted(decoder);
    bool complete = ur_decoder_state_is_terminal(state);

    if (weighted < 0.0 || weighted > 1.0) {
      fprintf(stderr, "❌ Frame %d: %.6f outside [0, 1]\n", i, weighted);
      ok = false;
    }
    if (!complete && weighted > 0.99) {
      fprintf(stderr, "❌ Frame %d: %.6f > 0.99 while incomplete\n", i,
              weighted);
      ok = false;
    }
    if (complete && weighted != 1.0) {
      fprintf(stderr, "❌ Frame %d: complete but reports %.6f\n", i, weighted);
      ok = false;
    }
    if (weighted + 1e-12 < prev) {
      fprintf(stderr, "❌ Frame %d: decreased from %.6f to %.6f\n", i, prev,
              weighted);
      ok = false;
    }
    prev = weighted;

    if (complete)
      break;
  }

  if (ok && ur_decoder_get_state(decoder) != UR_DECODER_OK) {
    fprintf(stderr, "❌ Decode did not complete after %d parts\n", parts_used);
    ok = false;
  }
  if (ok) {
    printf("✅ PASS - invariants held across %d parts (final %.4f)\n",
           parts_used, prev);
  }

  ur_decoder_free(decoder);
  free_fragments(fragments, fragment_count);
  return ok;
}

int main(int argc, char *argv[]) {
  if (ur_decoder_estimated_percent_complete_weighted(NULL) != 0.0) {
    fprintf(stderr, "❌ NULL decoder should report 0.0\n");
    return 1;
  }
  return run_test_suite(argc, argv, "UR Weighted Progress Test", TEST_CASES_DIR,
                        ".UR_fragments.txt", test_file);
}
