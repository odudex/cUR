/*
 * test_ur_output_descriptor_roundtrip.c
 *
 * Full roundtrip test starting from descriptor strings:
 *   descriptor string
 *     → output_from_descriptor_string()
 *     → output_to_cbor()
 *     → ur_encoder_new() + ur_encoder_next_part()
 *     → ur_decoder_receive_part()
 *     → output_from_cbor()
 *     → output_descriptor()
 *     → compare with original
 */

#define _POSIX_C_SOURCE 200809L
#include "../src/ur_decoder.h"
#include "../src/ur_encoder.h"
#include "../src/types/output.h"
#include "test_utils.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CASES_DIR "tests/test_cases/output"

static bool test_file(const char *filepath) {
  printf("\n=== Testing: %s ===\n", filepath);

  // Read descriptor from file
  char *descriptor = read_text_file_first_line(filepath);
  if (!descriptor) {
    fprintf(stderr, "Failed to read descriptor from: %s\n", filepath);
    return false;
  }
  printf("Input descriptor: %s\n", descriptor);

  // Step 1: Parse descriptor string into output_data_t
  output_data_t *output = output_from_descriptor_string(descriptor);
  if (!output) {
    fprintf(stderr, "Failed to parse descriptor string\n");
    free(descriptor);
    return false;
  }
  printf("Parsed descriptor successfully\n");

  // Step 2: Encode output to CBOR
  size_t cbor_len = 0;
  uint8_t *cbor_data = output_to_cbor(output, &cbor_len);
  if (!cbor_data || cbor_len == 0) {
    fprintf(stderr, "Failed to encode output to CBOR\n");
    output_free(output);
    free(descriptor);
    return false;
  }
  printf("CBOR encoded: %zu bytes\n", cbor_len);

  // Step 3: Create UR encoder
  size_t max_fragment_len = 200;
  ur_encoder_t *encoder =
      ur_encoder_new("crypto-output", cbor_data, cbor_len, max_fragment_len, 0, 10);
  if (!encoder) {
    fprintf(stderr, "Failed to create UR encoder\n");
    free(cbor_data);
    output_free(output);
    free(descriptor);
    return false;
  }
  printf("Encoder: single_part=%s, seq_len=%zu\n",
         ur_encoder_is_single_part(encoder) ? "yes" : "no",
         ur_encoder_seq_len(encoder));

  // Step 4: Create UR decoder and feed fragments
  ur_decoder_t *decoder = ur_decoder_new();
  if (!decoder) {
    fprintf(stderr, "Failed to create UR decoder\n");
    ur_encoder_free(encoder);
    free(cbor_data);
    output_free(output);
    free(descriptor);
    return false;
  }

  int parts_sent = 0;
  size_t max_parts = ur_encoder_seq_len(encoder) * 2 + 10;

  while (!ur_decoder_is_complete(decoder) && parts_sent < (int)max_parts) {
    char *part = NULL;
    if (!ur_encoder_next_part(encoder, &part)) {
      fprintf(stderr, "Failed to get next UR part\n");
      ur_decoder_free(decoder);
      ur_encoder_free(encoder);
      free(cbor_data);
      output_free(output);
      free(descriptor);
      return false;
    }
    ur_decoder_receive_part(decoder, part);
    free(part);
    parts_sent++;
  }

  if (!ur_decoder_is_complete(decoder) || !ur_decoder_is_success(decoder)) {
    fprintf(stderr, "UR decoding failed after %d parts\n", parts_sent);
    ur_decoder_free(decoder);
    ur_encoder_free(encoder);
    free(cbor_data);
    output_free(output);
    free(descriptor);
    return false;
  }
  printf("UR decode complete after %d parts\n", parts_sent);

  // Step 5: Get CBOR from decoded result and parse back to output
  ur_result_t *result = ur_decoder_get_result(decoder);
  if (!result) {
    fprintf(stderr, "Failed to get UR result\n");
    ur_decoder_free(decoder);
    ur_encoder_free(encoder);
    free(cbor_data);
    output_free(output);
    free(descriptor);
    return false;
  }

  output_data_t *decoded_output = output_from_cbor(result->cbor_data, result->cbor_len);
  if (!decoded_output) {
    fprintf(stderr, "Failed to decode output from roundtrip CBOR\n");
    ur_decoder_free(decoder);
    ur_encoder_free(encoder);
    free(cbor_data);
    output_free(output);
    free(descriptor);
    return false;
  }

  // Step 6: Generate descriptor from decoded output and compare
  char *roundtrip_descriptor = output_descriptor(decoded_output, true);
  if (!roundtrip_descriptor) {
    fprintf(stderr, "Failed to generate descriptor from decoded output\n");
    output_free(decoded_output);
    ur_decoder_free(decoder);
    ur_encoder_free(encoder);
    free(cbor_data);
    output_free(output);
    free(descriptor);
    return false;
  }

  printf("Roundtrip descriptor: %s\n", roundtrip_descriptor);

  bool success = (strcmp(descriptor, roundtrip_descriptor) == 0);
  if (success) {
    printf("PASS - Roundtrip successful\n");
  } else {
    printf("FAIL - Descriptor mismatch\n");
    printf("  Input:     %s\n", descriptor);
    printf("  Roundtrip: %s\n", roundtrip_descriptor);
  }

  free(roundtrip_descriptor);
  output_free(decoded_output);
  ur_decoder_free(decoder);
  ur_encoder_free(encoder);
  free(cbor_data);
  output_free(output);
  free(descriptor);

  return success;
}

int main(int argc, char *argv[]) {
  printf("=== UR Output Descriptor Roundtrip Test ===\n");

  const char *test_cases_dir = TEST_CASES_DIR;

  // Single file mode
  if (argc > 1) {
    char filepath[512];
    const char *arg = argv[1];

    if (strstr(arg, test_cases_dir) == arg) {
      snprintf(filepath, sizeof(filepath), "%s", arg);
    } else {
      snprintf(filepath, sizeof(filepath), "%s/%s", test_cases_dir, arg);
    }

    printf("Running single test: %s\n", filepath);
    bool result = test_file(filepath);
    printf("\n=== Summary ===\n");
    printf("Test %s\n", result ? "PASSED" : "FAILED");
    return result ? 0 : 1;
  }

  // Auto-discover *.descriptor.txt files
  DIR *dir = opendir(test_cases_dir);
  if (!dir) {
    fprintf(stderr, "Failed to open directory: %s\n", test_cases_dir);
    return 1;
  }

  struct dirent *entry;
  char **test_files = malloc(100 * sizeof(char *));
  int file_count = 0;

  while ((entry = readdir(dir)) != NULL) {
    if (strstr(entry->d_name, ".descriptor.txt")) {
      char filepath[512];
      snprintf(filepath, sizeof(filepath), "%s/%s", test_cases_dir, entry->d_name);
      test_files[file_count] = strdup(filepath);
      file_count++;
    }
  }
  closedir(dir);

  if (file_count == 0) {
    printf("No .descriptor.txt files found in %s\n", test_cases_dir);
    free(test_files);
    return 1;
  }

  int total = 0, passed = 0;
  for (int i = 0; i < file_count; i++) {
    total++;
    if (test_file(test_files[i])) {
      passed++;
    }
    free(test_files[i]);
  }
  free(test_files);

  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/%d\n", passed, total);

  return (passed == total) ? 0 : 1;
}
