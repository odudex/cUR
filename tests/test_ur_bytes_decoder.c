/*
 * test_ur_bytes_decoder.c
 *
 * Test URDecoder with bytes data
 * Reads .UR_fragments.txt files, decodes to bytes,
 * then verifies against expected data in .decoded.txt.
 */

#define _POSIX_C_SOURCE 200809L
#include "../src/ur.h"
#include "../src/ur_decoder.h"
#include "../src/types/bytes_type.h"
#include "../src/utils.h"
#include "test_utils.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define TEST_CASES_DIR "tests/test_cases/bytes"

// Function to test a single file
int test_file(const char *filepath) {
  printf("\n=== Testing file: %s ===\n", filepath);

  int fragment_count = 0;
  char **fragments = read_fragments_from_file(filepath, &fragment_count);

  if (!fragments || fragment_count == 0) {
    fprintf(stderr, "❌ No fragments found in file: %s\n", filepath);
    return 1;
  }

  printf("Found %d fragments\n", fragment_count);

  // Generate expected data filename
  char expected_path[512];
  strncpy(expected_path, filepath, sizeof(expected_path) - 1);
  expected_path[sizeof(expected_path) - 1] = '\0';

  char *ext = strstr(expected_path, ".UR_fragments.txt");
  if (ext) {
    strcpy(ext, ".decoded.txt");
  } else {
    free_fragments(fragments, fragment_count);
    fprintf(stderr, "❌ Invalid filename format\n");
    return 1;
  }

  // Read expected data
  size_t expected_len = 0;
  uint8_t *expected_data = read_binary_file(expected_path, &expected_len);
  if (!expected_data) {
    fprintf(stderr, "❌ Failed to read expected data: %s\n", expected_path);
    free_fragments(fragments, fragment_count);
    return 1;
  }
  printf("Expected data length: %zu bytes\n", expected_len);

  // Create decoder
  ur_decoder_t *decoder = ur_decoder_new();
  if (!decoder) {
    fprintf(stderr, "❌ Failed to create UR decoder\n");
    free_fragments(fragments, fragment_count);
    free(expected_data);
    return 1;
  }

  int success = 0;
  int parts_used = 0;

  // Feed all fragments
  for (int i = 0; i < fragment_count; i++) {
    if (ur_decoder_receive_part(decoder, fragments[i])) {
      parts_used++;

      if (ur_decoder_is_complete(decoder)) {
        printf("Decoder complete after %d parts\n", parts_used);
        break;
      }
    }
  }

  if (ur_decoder_is_complete(decoder) && ur_decoder_is_success(decoder)) {
    printf("✓ Decoding successful\n");

    // Get the UR result
    ur_result_t *result = ur_decoder_get_result(decoder);
    if (result) {
      printf("UR type: %s\n", result->type);

      // Get CBOR data
      const uint8_t *cbor_data = result->cbor_data;
      size_t cbor_len = result->cbor_len;

      if (cbor_data && cbor_len > 0) {
        printf("CBOR length: %zu bytes\n", cbor_len);

        // Decode bytes from CBOR
        bytes_data_t *bytes = bytes_from_cbor(cbor_data, cbor_len);

        if (bytes) {
          // Get bytes data
          size_t bytes_len;
          const uint8_t *bytes_data = bytes_get_data(bytes, &bytes_len);

          printf("Actual data length: %zu bytes\n", bytes_len);

          if (bytes_data && bytes_len > 0) {
            // Compare with expected data
            if (bytes_len == expected_len && memcmp(bytes_data, expected_data, bytes_len) == 0) {
              printf("✅ PASS - Bytes data matches expected\n");
              success = 1;
            } else {
              printf("❌ FAIL - Bytes data mismatch\n");
              printf("Expected length: %zu, Actual length: %zu\n", expected_len, bytes_len);

              // Show first bytes that differ
              if (bytes_len != expected_len) {
                printf("Length mismatch!\n");
              } else {
                for (size_t i = 0; i < bytes_len; i++) {
                  if (bytes_data[i] != expected_data[i]) {
                    printf("First mismatch at byte %zu: expected 0x%02x, got 0x%02x\n",
                           i, expected_data[i], bytes_data[i]);
                    break;
                  }
                }
              }
            }
          } else {
            fprintf(stderr, "❌ Failed to get bytes data\n");
          }

          bytes_free(bytes);
        } else {
          fprintf(stderr, "❌ Failed to decode bytes from CBOR\n");
        }
      } else {
        fprintf(stderr, "❌ Failed to get CBOR data\n");
      }
    } else {
      fprintf(stderr, "❌ Failed to get UR result\n");
    }
  } else {
    printf("❌ Decoding failed or incomplete\n");
  }

  ur_decoder_free(decoder);
  free_fragments(fragments, fragment_count);
  free(expected_data);

  return success ? 0 : 1;
}

int main(int argc, char *argv[]) {
  printf("=== UR Decoder Test (bytes) ===\n");

  // Check if a specific test vector file was provided
  if (argc > 1) {
    const char *test_vector = argv[1];
    char filepath[512];

    // Check if the path already includes the directory
    if (strstr(test_vector, TEST_CASES_DIR) == test_vector) {
      snprintf(filepath, sizeof(filepath), "%s", test_vector);
    } else {
      snprintf(filepath, sizeof(filepath), "%s/%s", TEST_CASES_DIR,
               test_vector);
    }

    printf("Running single test: %s\n", filepath);
    int result = test_file(filepath);

    printf("\n=== Summary ===\n");
    printf("Test %s\n", result == 0 ? "PASSED" : "FAILED");

    return result;
  }

  // Run all tests
  DIR *dir = opendir(TEST_CASES_DIR);
  if (!dir) {
    fprintf(stderr, "Failed to open directory: %s\n", TEST_CASES_DIR);
    return 1;
  }

  struct dirent *entry;
  int total_tests = 0;
  int passed_tests = 0;

  // First pass: count files
  char **test_files = (char **)malloc(100 * sizeof(char *));
  int file_count = 0;

  while ((entry = readdir(dir)) != NULL) {
    if (strstr(entry->d_name, ".UR_fragments.txt")) {
      char filepath[512];
      snprintf(filepath, sizeof(filepath), "%s/%s", TEST_CASES_DIR,
               entry->d_name);
      test_files[file_count] = strdup(filepath);
      file_count++;
    }
  }
  closedir(dir);

  // Process each file
  for (int i = 0; i < file_count; i++) {
    total_tests++;
    if (test_file(test_files[i]) == 0) {
      passed_tests++;
    }
    free(test_files[i]);
  }
  free(test_files);

  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/%d\n", passed_tests, total_tests);

  return (passed_tests == total_tests) ? 0 : 1;
}
