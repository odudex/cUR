#include "../src/ur.h"
#include "../src/ur_decoder.h"
#include "../src/utils.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define MAX_FRAGMENTS 100
#define MAX_LINE_LENGTH 1024
#define TEST_CASES_DIR "tests/test_cases"

void print_hex_data(const char *label, const uint8_t *data, size_t len) {
  printf("%s (%zu bytes): ", label, len);
  for (size_t i = 0; i < len; i++) {
    printf("%02x", data[i]);
  }
  printf("\n");
}

void print_base64_data(const char *label, const uint8_t *data, size_t len) {
  // Calculate the length of the base64 encoded string
  size_t encoded_len = 4 * ((len + 2) / 3);
  char *encoded = (char *)malloc(encoded_len + 1);
  if (!encoded) {
    fprintf(stderr, "Memory allocation failed\n");
    return;
  }

  // Encode to base64
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

  printf("%s (base64): %s\n", label, encoded);
  free(encoded);
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

    // Find the start of the UR string (case-insensitive, after quotes if
    // present)
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

// Function to read expected output from file
uint8_t *read_expected_output(const char *filepath, size_t *output_len) {
  FILE *file = fopen(filepath, "r");
  if (!file) {
    return NULL;
  }

  char line[65536]; // Large buffer for hex strings
  if (!fgets(line, sizeof(line), file)) {
    fclose(file);
    return NULL;
  }
  fclose(file);

  // Remove newline
  line[strcspn(line, "\r\n")] = 0;

  // Check for "hex:" prefix
  char *hex_start = line;
  if (strncmp(line, "hex:", 4) == 0) {
    hex_start = line + 4;
  }

  // Convert hex string to bytes
  size_t hex_len = strlen(hex_start);
  if (hex_len % 2 != 0) {
    return NULL; // Invalid hex string
  }

  size_t byte_len = hex_len / 2;
  uint8_t *bytes = (uint8_t *)malloc(byte_len);
  if (!bytes) {
    return NULL;
  }

  for (size_t i = 0; i < byte_len; i++) {
    int value;
    if (sscanf(hex_start + (i * 2), "%2x", &value) != 1) {
      free(bytes);
      return NULL;
    }
    bytes[i] = (uint8_t)value;
  }

  *output_len = byte_len;
  return bytes;
}

// Function to test a single file
int test_file(const char *filepath) {
  printf("\n=== Testing file: %s ===\n", filepath);

  int fragment_count = 0;
  char **fragments = read_fragments_from_file(filepath, &fragment_count);

  if (!fragments || fragment_count == 0) {
    fprintf(stderr, "No fragments found in file: %s\n", filepath);
    return 1;
  }

  // Generate expected output filename
  char expected_path[512];
  strncpy(expected_path, filepath, sizeof(expected_path) - 1);
  expected_path[sizeof(expected_path) - 1] = '\0';

  char *ext = strstr(expected_path, ".UR_fragments.txt");
  if (ext) {
    strcpy(ext, ".UR_object.txt");
  }

  // Read expected output if it exists
  size_t expected_len = 0;
  uint8_t *expected_data = read_expected_output(expected_path, &expected_len);

  ur_decoder_t *decoder = ur_decoder_new();
  if (!decoder) {
    fprintf(stderr, "Failed to create UR decoder\n");
    free_fragments(fragments, fragment_count);
    if (expected_data)
      free(expected_data);
    return 1;
  }

  int success = 0;
  int parts_used = 0;

  for (int i = 0; i < fragment_count; i++) {
    bool result = ur_decoder_receive_part(decoder, fragments[i]);

    if (!result) {
      continue;
    }

    parts_used++;

    if (ur_decoder_is_complete(decoder)) {
      printf("Data reconstruction is complete: YES\n");

      if (ur_decoder_is_success(decoder)) {
        printf("Data reconstruction is success (valid): YES\n");
        success = 1;

        // Verify against expected output if available
        if (expected_data) {
          ur_result_t *result = ur_decoder_get_result(decoder);
          if (result) {
            if (result->cbor_len == expected_len &&
                memcmp(result->cbor_data, expected_data, result->cbor_len) ==
                    0) {
              printf("Output matches expected: YES\n");
            } else {
              printf("Output matches expected: NO\n");
              printf("  Expected length: %zu, Got length: %zu\n", expected_len,
                     result->cbor_len);
              success = 0;
            }
          } else {
            printf("Output matches expected: NO (failed to get result)\n");
            success = 0;
          }
        }
      } else {
        printf("Data reconstruction is success (valid): NO\n");
      }

      printf("Parts used/total available parts: %d/%d\n", parts_used,
             fragment_count);
      break;
    }
  }

  if (!ur_decoder_is_complete(decoder)) {
    printf("Data reconstruction is complete: NO\n");
    printf("Data reconstruction is success (valid): NO\n");
    printf("Parts used/total available parts: %d/%d\n", parts_used,
           fragment_count);
  }

  ur_decoder_free(decoder);
  free_fragments(fragments, fragment_count);
  if (expected_data)
    free(expected_data);

  return success ? 0 : 1;
}

int main(int argc, char *argv[]) {
  printf("=== C UR Implementation Test ===\n");

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

  // No argument provided, run all tests in the directory
  DIR *dir = opendir(TEST_CASES_DIR);
  if (!dir) {
    fprintf(stderr, "Failed to open directory: %s\n", TEST_CASES_DIR);
    return 1;
  }

  struct dirent *entry;
  int total_tests = 0;
  int passed_tests = 0;

  while ((entry = readdir(dir)) != NULL) {
    // Skip . and .. directories
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    // Only process .UR_fragments.txt files
    const char *suffix = ".UR_fragments.txt";
    size_t name_len = strlen(entry->d_name);
    size_t suffix_len = strlen(suffix);

    if (name_len <= suffix_len ||
        strcmp(entry->d_name + name_len - suffix_len, suffix) != 0) {
      continue;
    }

    // Build full path
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", TEST_CASES_DIR,
             entry->d_name);

    total_tests++;
    if (test_file(filepath) == 0) {
      passed_tests++;
    }
  }

  closedir(dir);

  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/%d\n", passed_tests, total_tests);

  return (passed_tests == total_tests) ? 0 : 1;
}