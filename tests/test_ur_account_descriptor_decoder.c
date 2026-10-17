/*
 * test_ur_account_descriptor_decoder.c
 *
 * Test URDecoder with crypto-account data
 * Reads .UR_fragments.txt files, decodes to crypto-account, then checks
 * every output descriptor from output_descriptors_from_cbor_account()
 * against the .descriptor.txt file (one per line, in order), and the first
 * one from output_descriptor_from_cbor_account().
 */

#include "../src/types/output.h"
#include "../src/ur.h"
#include "../src/ur_decoder.h"
#include "../src/utils.h"
#include "test_harness.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CASES_DIR "tests/test_cases/account"
#define MAX_OUTPUTS 16

// Read up to MAX_OUTPUTS non-empty lines. Returns the count, or -1.
static int read_lines(const char *path, char lines[MAX_OUTPUTS][1024]) {
  FILE *file = fopen(path, "r");
  if (!file)
    return -1;
  int n = 0;
  while (n < MAX_OUTPUTS && fgets(lines[n], sizeof lines[n], file)) {
    lines[n][strcspn(lines[n], "\r\n")] = '\0';
    if (lines[n][0])
      n++;
  }
  fclose(file);
  return n;
}

// Compare every decoded output, and the first-output call, with `expected`.
static bool check_outputs(const uint8_t *cbor, size_t len,
                          char expected[MAX_OUTPUTS][1024], int n) {
  bool ok = true;
  char *first = output_descriptor_from_cbor_account(cbor, len);
  if (!first || strcmp(first, expected[0]) != 0) {
    printf("FAIL - first output: %s\n", first ? first : "(NULL)");
    ok = false;
  }
  free(first);

  size_t count = 0;
  char **all = output_descriptors_from_cbor_account(cbor, len, &count);
  if (!all || count != (size_t)n) {
    printf("FAIL - %zu outputs, expected %d\n", count, n);
    ok = false;
  }
  for (size_t i = 0; all && i < count && i < (size_t)n; i++) {
    printf("Output %zu: %s\n", i, all[i] ? all[i] : "(NULL)");
    if (!all[i] || strcmp(all[i], expected[i]) != 0) {
      printf("FAIL - expected: %s\n", expected[i]);
      ok = false;
    }
  }
  output_descriptors_free(all, count);
  return ok;
}

static bool test_file(const char *filepath) {
  printf("\n=== Testing file: %s ===\n", filepath);

  int fragment_count = 0;
  char **fragments = read_fragments_from_file(filepath, &fragment_count);

  if (!fragments || fragment_count == 0) {
    fprintf(stderr, "Failed to read fragments from file: %s\n", filepath);
    return false;
  }

  printf("Found %d fragments\n", fragment_count);

  // Generate expected descriptor filename
  char expected_path[512];
  strncpy(expected_path, filepath, sizeof(expected_path) - 1);
  expected_path[sizeof(expected_path) - 1] = '\0';

  char *ext = strstr(expected_path, ".UR_fragments.txt");
  if (ext) {
    strcpy(ext, ".descriptor.txt");
  } else {
    free_fragments(fragments, fragment_count);
    fprintf(stderr, "Invalid filename format\n");
    return false;
  }

  // Read expected descriptors
  static char expected[MAX_OUTPUTS][1024];
  int expected_count = read_lines(expected_path, expected);
  if (expected_count <= 0) {
    fprintf(stderr, "Failed to read expected descriptors: %s\n", expected_path);
    free_fragments(fragments, fragment_count);
    return false;
  }
  printf("Expected %d descriptors, first: %s\n", expected_count, expected[0]);

  // Create decoder
  ur_decoder_t *decoder = ur_decoder_new();
  if (!decoder) {
    fprintf(stderr, "Failed to create UR decoder\n");
    free_fragments(fragments, fragment_count);
    return false;
  }

  bool success = false;
  int parts_used = 0;

  // Feed all fragments
  for (int i = 0; i < fragment_count; i++) {
    ur_decoder_state_t state = ur_decoder_receive_part(decoder, fragments[i]);
    if (!ur_decoder_state_is_error(state)) {
      parts_used++;
    }
    if (ur_decoder_state_is_terminal(state)) {
      printf("Decoder complete after %d parts\n", parts_used);
      break;
    }
  }

  if (ur_decoder_get_state(decoder) == UR_DECODER_OK) {
    printf("Decoding successful\n");

    // Get the UR result
    ur_result_t *result = ur_decoder_get_result(decoder);
    if (result) {
      printf("UR type: %s\n", result->type);

      // Get CBOR data
      const uint8_t *cbor_data = result->cbor_data;
      size_t cbor_len = result->cbor_len;

      if (cbor_data && cbor_len > 0) {
        printf("CBOR length: %zu bytes\n", cbor_len);

        success = check_outputs(cbor_data, cbor_len, expected, expected_count);
        if (success)
          printf("PASS - Descriptors match expected\n");
      } else {
        fprintf(stderr, "Failed to get CBOR data\n");
      }
    } else {
      fprintf(stderr, "Failed to get UR result\n");
    }
  } else {
    printf("Decoding failed or incomplete\n");
  }

  ur_decoder_free(decoder);
  free_fragments(fragments, fragment_count);

  return success;
}

int main(int argc, char *argv[]) {
  return run_test_suite(argc, argv, "UR Account Descriptor Decoder Test",
                        TEST_CASES_DIR, ".UR_fragments.txt", test_file);
}
