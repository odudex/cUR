#define _POSIX_C_SOURCE 200809L
#include "../src/ur.h"
#include "../src/ur_decoder.h"
#include "../src/types/psbt.h"
#include "../src/utils.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define MAX_FRAGMENTS 1000
#define MAX_LINE_LENGTH 2048
#define TEST_CASES_DIR "tests/test_cases/PSBTs"

// Function to encode data to base64
char *base64_encode(const uint8_t *data, size_t len) {
  size_t encoded_len = 4 * ((len + 2) / 3);
  char *encoded = (char *)malloc(encoded_len + 1);
  if (!encoded) {
    return NULL;
  }

  static const char b64_table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (size_t i = 0, j = 0; i < len;) {
    uint32_t octet_a = i < len ? data[i++] : 0;
    uint32_t octet_b = i < len ? data[i++] : 0;
    uint32_t octet_c = i < len ? data[i++] : 0;

    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    encoded[j++] = b64_table[(triple >> 18) & 0x3F];
    encoded[j++] = b64_table[(triple >> 12) & 0x3F];
    encoded[j++] = (i > len + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
    encoded[j++] = (i > len) ? '=' : b64_table[triple & 0x3F];
  }
  encoded[encoded_len] = '\0';

  return encoded;
}

// Function to read fragments from a file
char **read_fragments_from_file(const char *filepath, int *count) {
  FILE *file = fopen(filepath, "r");
  if (!file) {
    fprintf(stderr, "Failed to open file: %s\n", filepath);
    return NULL;
  }

  char **fragments = (char **)malloc(MAX_FRAGMENTS * sizeof(char *));
  if (!fragments) {
    fclose(file);
    return NULL;
  }

  char line[MAX_LINE_LENGTH];
  *count = 0;

  while (fgets(line, sizeof(line), file) && *count < MAX_FRAGMENTS) {
    // Remove newline
    line[strcspn(line, "\r\n")] = 0;

    // Skip empty lines
    if (strlen(line) == 0) {
      continue;
    }

    // Find the start of the UR string (case-insensitive)
    char *start = NULL;
    char *pos = line;
    while (*pos) {
      if ((pos[0] == 'u' || pos[0] == 'U') &&
          (pos[1] == 'r' || pos[1] == 'R') && pos[2] == ':') {
        start = pos;
        break;
      }
      pos++;
    }

    if (!start)
      continue;

    // Find the end (before quote or comma if present)
    char *end = start;
    while (*end && *end != '"' && *end != ',' && *end != '\r' && *end != '\n') {
      end++;
    }

    size_t len = end - start;
    if (len > 0) {
      fragments[*count] = (char *)malloc(len + 1);
      if (!fragments[*count]) {
        // Cleanup on failure
        for (int i = 0; i < *count; i++) {
          free(fragments[i]);
        }
        free(fragments);
        fclose(file);
        return NULL;
      }

      strncpy(fragments[*count], start, len);
      fragments[*count][len] = '\0';
      (*count)++;
    }
  }

  fclose(file);
  return fragments;
}

// Function to free fragments array
void free_fragments(char **fragments, int count) {
  if (!fragments)
    return;
  for (int i = 0; i < count; i++) {
    free(fragments[i]);
  }
  free(fragments);
}

// Function to read expected PSBT bytes from binary file
uint8_t *read_expected_psbt_bytes(const char *filepath, size_t *out_len) {
  FILE *file = fopen(filepath, "rb");
  if (!file) {
    return NULL;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0) {
    fclose(file);
    return NULL;
  }

  // Allocate buffer
  uint8_t *bytes = (uint8_t *)malloc(file_size);
  if (!bytes) {
    fclose(file);
    return NULL;
  }

  // Read file
  size_t read_size = fread(bytes, 1, file_size, file);
  fclose(file);

  if (read_size != (size_t)file_size) {
    free(bytes);
    return NULL;
  }

  *out_len = file_size;
  return bytes;
}

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

  // Generate expected binary filename
  char expected_path[512];
  strncpy(expected_path, filepath, sizeof(expected_path) - 1);
  expected_path[sizeof(expected_path) - 1] = '\0';

  char *ext = strstr(expected_path, ".UR_fragments.txt");
  if (ext) {
    strcpy(ext, ".psbt.bin");
  } else {
    free_fragments(fragments, fragment_count);
    fprintf(stderr, "❌ Invalid filename format\n");
    return 1;
  }

  // Read expected PSBT bytes
  size_t expected_len = 0;
  uint8_t *expected_bytes = read_expected_psbt_bytes(expected_path, &expected_len);
  if (!expected_bytes) {
    fprintf(stderr, "❌ Failed to read expected PSBT bytes: %s\n", expected_path);
    free_fragments(fragments, fragment_count);
    return 1;
  }
  printf("Expected PSBT length: %zu bytes\n", expected_len);

  // Create decoder
  ur_decoder_t *decoder = ur_decoder_new();
  if (!decoder) {
    fprintf(stderr, "❌ Failed to create UR decoder\n");
    free_fragments(fragments, fragment_count);
    free(expected_bytes);
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
        // Decode PSBT from CBOR
        psbt_data_t *psbt = psbt_from_cbor(cbor_data, cbor_len);

        if (psbt) {
          // Get PSBT data
          size_t psbt_len;
          const uint8_t *psbt_data = psbt_get_data(psbt, &psbt_len);

          printf("Actual PSBT length: %zu bytes\n", psbt_len);

          if (psbt_data && psbt_len > 0) {
            // Compare with expected bytes
            if (psbt_len == expected_len && memcmp(psbt_data, expected_bytes, psbt_len) == 0) {
              printf("✅ PASS - PSBT bytes match expected\n");
              success = 1;
            } else {
              printf("❌ FAIL - PSBT bytes mismatch\n");
              printf("Expected length: %zu, Actual length: %zu\n", expected_len, psbt_len);

              // Show first bytes that differ
              if (psbt_len != expected_len) {
                printf("Length mismatch!\n");
              } else {
                for (size_t i = 0; i < psbt_len; i++) {
                  if (psbt_data[i] != expected_bytes[i]) {
                    printf("First mismatch at byte %zu: expected 0x%02x, got 0x%02x\n",
                           i, expected_bytes[i], psbt_data[i]);
                    break;
                  }
                }
              }
            }
          } else {
            fprintf(stderr, "❌ Failed to get PSBT data\n");
          }

          psbt_free(psbt);
        } else {
          fprintf(stderr, "❌ Failed to decode PSBT from CBOR\n");
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
  free(expected_bytes);

  return success ? 0 : 1;
}

int main(int argc, char *argv[]) {
  printf("=== UR Decoder Test (PSBT) ===\n");

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
