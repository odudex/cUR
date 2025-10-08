/*
 * test_encoder_with_testcases.c
 *
 * Test UREncoder with real test cases from test_cases/ directory
 * Reads UR_object.txt files (hex CBOR data), encodes with UREncoder,
 * then decodes with URDecoder to verify roundtrip.
 */

#include "../src/ur_decoder.h"
#include "../src/ur_encoder.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper: Convert hex string to bytes
static bool hex_to_bytes(const char *hex, uint8_t **bytes_out,
                         size_t *len_out) {
  // Skip "hex:" prefix if present
  if (strncmp(hex, "hex:", 4) == 0) {
    hex += 4;
  }

  // Count hex digits (skip whitespace and newlines)
  size_t hex_len = 0;
  for (const char *p = hex; *p; p++) {
    if (isxdigit(*p)) {
      hex_len++;
    }
  }

  if (hex_len % 2 != 0) {
    fprintf(stderr, "Invalid hex string (odd length)\n");
    return false;
  }

  size_t byte_len = hex_len / 2;
  uint8_t *bytes = (uint8_t *)malloc(byte_len);
  if (!bytes) {
    return false;
  }

  size_t byte_idx = 0;
  const char *p = hex;
  while (*p && byte_idx < byte_len) {
    // Skip whitespace
    while (*p && isspace(*p))
      p++;
    if (!*p)
      break;

    // Read two hex digits
    if (!isxdigit(p[0]) || !isxdigit(p[1])) {
      free(bytes);
      return false;
    }

    char hex_byte[3] = {p[0], p[1], 0};
    bytes[byte_idx++] = (uint8_t)strtol(hex_byte, NULL, 16);
    p += 2;
  }

  *bytes_out = bytes;
  *len_out = byte_idx;
  return true;
}

// Helper: Read hex CBOR data from file
static bool read_cbor_from_file(const char *filename, uint8_t **cbor_out,
                                size_t *len_out) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    fprintf(stderr, "Cannot open file: %s\n", filename);
    return false;
  }

  // Read entire file
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *content = (char *)malloc(file_size + 1);
  if (!content) {
    fclose(f);
    return false;
  }

  size_t read_size = fread(content, 1, file_size, f);
  content[read_size] = '\0';
  fclose(f);

  // Convert hex to bytes
  bool success = hex_to_bytes(content, cbor_out, len_out);
  free(content);

  return success;
}

// Test one file: encode with UREncoder, decode with URDecoder, verify roundtrip
static bool test_file(const char *filename) {
  printf("\n=== Testing: %s ===\n", filename);

  // Read CBOR data from file
  uint8_t *original_cbor = NULL;
  size_t original_len = 0;

  if (!read_cbor_from_file(filename, &original_cbor, &original_len)) {
    printf("✗ Failed to read file\n");
    return false;
  }

  printf("Original CBOR size: %zu bytes\n", original_len);

  // Create UREncoder
  size_t max_fragment_len = 200; // Reasonable fragment size
  ur_encoder_t *encoder = ur_encoder_new("crypto-psbt", original_cbor,
                                         original_len, max_fragment_len, 0, 10);

  if (!encoder) {
    printf("✗ Failed to create encoder\n");
    free(original_cbor);
    return false;
  }

  printf("Encoder created:\n");
  printf("  - Is single part: %s\n",
         ur_encoder_is_single_part(encoder) ? "yes" : "no");
  printf("  - Sequence length: %zu\n", ur_encoder_seq_len(encoder));

  // Create URDecoder
  ur_decoder_t *decoder = ur_decoder_new();
  if (!decoder) {
    printf("✗ Failed to create decoder\n");
    ur_encoder_free(encoder);
    free(original_cbor);
    return false;
  }

  // Generate parts and feed to decoder
  int parts_sent = 0;
  size_t expected_parts = ur_encoder_seq_len(encoder);
  size_t max_parts =
      expected_parts * 2 + 10; // Allow for fountain coding overhead

  printf("Encoding and decoding:\n");

  while (!ur_decoder_is_complete(decoder) && parts_sent < (int)max_parts) {
    char *part = NULL;
    if (!ur_encoder_next_part(encoder, &part)) {
      printf("✗ Failed to generate part %d\n", parts_sent + 1);
      break;
    }

    // Show first few parts
    if (parts_sent < 3) {
      printf("  Part %d: %.60s...\n", parts_sent + 1, part);
    }

    bool received = ur_decoder_receive_part(decoder, part);
    free(part);
    parts_sent++;

    if (!received && parts_sent <= (int)expected_parts) {
      printf("  Warning: Part %d not received by decoder\n", parts_sent);
    }

    if (ur_decoder_is_complete(decoder)) {
      printf("  Decoder completed after %d parts (expected ~%zu)\n", parts_sent,
             expected_parts);
      break;
    }
  }

  // Check results
  bool test_passed = false;

  if (!ur_decoder_is_complete(decoder)) {
    printf("✗ Decoder did not complete after %d parts\n", parts_sent);
    printf("  Expected parts: %zu\n", ur_decoder_expected_part_count(decoder));
    printf("  Processed parts: %zu\n",
           ur_decoder_processed_parts_count(decoder));
  } else if (!ur_decoder_is_success(decoder)) {
    printf("✗ Decoder completed but not successful\n");
  } else {
    ur_result_t *result = ur_decoder_get_result(decoder);
    if (!result) {
      printf("✗ No result from decoder\n");
    } else {
      printf("Decoded result:\n");
      printf("  - Type: %s\n", result->type);
      printf("  - CBOR size: %zu bytes (original: %zu)\n", result->cbor_len,
             original_len);

      // Verify data matches
      if (result->cbor_len == original_len &&
          memcmp(result->cbor_data, original_cbor, original_len) == 0) {
        printf("✓ DATA MATCHES PERFECTLY!\n");
        printf("✓ Test PASSED: Encode/Decode roundtrip successful\n");
        test_passed = true;
      } else {
        printf("✗ Data mismatch:\n");
        printf("  Expected %zu bytes, got %zu bytes\n", original_len,
               result->cbor_len);

        // Show first few bytes for debugging
        printf("  Original: ");
        for (size_t i = 0; i < (original_len < 20 ? original_len : 20); i++) {
          printf("%02x ", original_cbor[i]);
        }
        printf("%s\n", original_len > 20 ? "..." : "");

        printf("  Decoded:  ");
        for (size_t i = 0; i < (result->cbor_len < 20 ? result->cbor_len : 20);
             i++) {
          printf("%02x ", result->cbor_data[i]);
        }
        printf("%s\n", result->cbor_len > 20 ? "..." : "");
      }
    }
  }

  // Cleanup
  ur_encoder_free(encoder);
  ur_decoder_free(decoder);
  free(original_cbor);

  return test_passed;
}

int main() {
  printf("====================================================================="
         "=\n");
  printf("UREncoder Test with Real Test Cases\n");
  printf("====================================================================="
         "=\n");

  const char *test_dir = "tests/test_cases";
  DIR *dir = opendir(test_dir);

  if (!dir) {
    fprintf(stderr, "Cannot open test_cases directory\n");
    return 1;
  }

  // Collect all UR_object.txt files
  char **test_files = NULL;
  size_t test_count = 0;
  size_t capacity = 10;

  test_files = (char **)malloc(capacity * sizeof(char *));

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    // Look for files ending with .UR_object.txt
    size_t name_len = strlen(entry->d_name);
    if (name_len > 14 &&
        strcmp(entry->d_name + name_len - 14, ".UR_object.txt") == 0) {
      if (test_count >= capacity) {
        capacity *= 2;
        test_files = (char **)realloc(test_files, capacity * sizeof(char *));
      }

      // Build full path
      char *full_path =
          (char *)malloc(strlen(test_dir) + strlen(entry->d_name) + 2);
      sprintf(full_path, "%s/%s", test_dir, entry->d_name);
      test_files[test_count++] = full_path;
    }
  }
  closedir(dir);

  if (test_count == 0) {
    printf("No UR_object.txt files found in %s\n", test_dir);
    free(test_files);
    return 1;
  }

  printf("Found %zu test files\n", test_count);

  // Run tests
  size_t passed = 0;
  size_t failed = 0;

  for (size_t i = 0; i < test_count; i++) {
    if (test_file(test_files[i])) {
      passed++;
    } else {
      failed++;
    }
    free(test_files[i]);
  }

  free(test_files);

  // Summary
  printf("\n==================================================================="
         "===\n");
  printf("Test Summary\n");
  printf("====================================================================="
         "=\n");
  printf("Total tests:  %zu\n", test_count);
  printf("Passed:       %zu\n", passed);
  printf("Failed:       %zu\n", failed);
  printf("Success rate: %.1f%%\n", (double)passed / test_count * 100.0);
  printf("====================================================================="
         "=\n");

  if (failed == 0) {
    printf("✓ ALL TESTS PASSED!\n");
    return 0;
  } else {
    printf("✗ Some tests failed\n");
    return 1;
  }
}
