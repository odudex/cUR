/*
 * test_ur_PSBT_encoder.c
 *
 * Test UREncoder with PSBT data
 * Reads .psbt.bin files, encodes to UR fragments,
 * then decodes back to verify roundtrip.
 */

#define _POSIX_C_SOURCE 200809L
#include "../src/ur_decoder.h"
#include "../src/ur_encoder.h"
#include "../src/types/psbt.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Base64 decode table
static const int b64_decode_table[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1,
    -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1};

// Function to decode base64
static bool base64_decode(const char *input, uint8_t **output, size_t *out_len) {
  size_t input_len = strlen(input);
  if (input_len == 0)
    return false;

  // Calculate output length
  size_t max_output_len = (input_len / 4) * 3;
  uint8_t *decoded = (uint8_t *)malloc(max_output_len);
  if (!decoded)
    return false;

  size_t output_idx = 0;
  uint32_t buffer = 0;
  int bits_in_buffer = 0;

  for (size_t i = 0; i < input_len; i++) {
    unsigned char c = input[i];

    // Skip whitespace
    if (isspace(c))
      continue;

    // End of data
    if (c == '=')
      break;

    int value = b64_decode_table[c];
    if (value == -1) {
      free(decoded);
      return false;
    }

    buffer = (buffer << 6) | value;
    bits_in_buffer += 6;

    if (bits_in_buffer >= 8) {
      bits_in_buffer -= 8;
      decoded[output_idx++] = (buffer >> bits_in_buffer) & 0xFF;
    }
  }

  *output = decoded;
  *out_len = output_idx;
  return true;
}

// Function to encode data to base64
static char *base64_encode(const uint8_t *data, size_t len) {
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

// Helper: Read PSBT bytes from binary file
static bool read_psbt_from_file(const char *filename, uint8_t **bytes_out, size_t *len_out) {
  FILE *f = fopen(filename, "rb");
  if (!f) {
    fprintf(stderr, "Cannot open file: %s\n", filename);
    return false;
  }

  // Get file size
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (file_size <= 0) {
    fclose(f);
    return false;
  }

  // Allocate buffer
  uint8_t *bytes = (uint8_t *)malloc(file_size);
  if (!bytes) {
    fclose(f);
    return false;
  }

  // Read file
  size_t read_size = fread(bytes, 1, file_size, f);
  fclose(f);

  if (read_size != (size_t)file_size) {
    free(bytes);
    return false;
  }

  *bytes_out = bytes;
  *len_out = file_size;
  return true;
}

// Test one file: encode base64 PSBT to UR, decode back, verify roundtrip
static bool test_file(const char *filename) {
  printf("\n=== Testing: %s ===\n", filename);

  // Read PSBT bytes from binary file
  uint8_t *original_psbt_bytes = NULL;
  size_t original_psbt_len = 0;

  if (!read_psbt_from_file(filename, &original_psbt_bytes, &original_psbt_len)) {
    printf("❌ Failed to read file\n");
    return false;
  }

  printf("PSBT size: %zu bytes\n", original_psbt_len);

  // Create PSBT object (psbt_new makes a copy, so we'll keep original for comparison)
  psbt_data_t *psbt = psbt_new(original_psbt_bytes, original_psbt_len);

  if (!psbt) {
    printf("❌ Failed to create PSBT object\n");
    free(original_psbt_bytes);
    return false;
  }

  // Encode PSBT to CBOR
  size_t cbor_len = 0;
  uint8_t *cbor_data = psbt_to_cbor(psbt, &cbor_len);

  if (!cbor_data) {
    printf("❌ Failed to encode PSBT to CBOR\n");
    psbt_free(psbt);
    free(original_psbt_bytes);
    return false;
  }

  printf("CBOR size: %zu bytes\n", cbor_len);

  // Create UREncoder
  size_t max_fragment_len = 200; // Reasonable fragment size
  ur_encoder_t *encoder =
      ur_encoder_new("crypto-psbt", cbor_data, cbor_len, max_fragment_len, 0, 10);

  if (!encoder) {
    printf("❌ Failed to create encoder\n");
    free(cbor_data);
    psbt_free(psbt);
    free(original_psbt_bytes);
    return false;
  }

  printf("Encoder created:\n");
  printf("  - Is single part: %s\n",
         ur_encoder_is_single_part(encoder) ? "yes" : "no");
  printf("  - Sequence length: %zu\n", ur_encoder_seq_len(encoder));

  // Create URDecoder
  ur_decoder_t *decoder = ur_decoder_new();
  if (!decoder) {
    printf("❌ Failed to create decoder\n");
    ur_encoder_free(encoder);
    free(cbor_data);
    psbt_free(psbt);
    free(original_psbt_bytes);
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
      printf("❌ Failed to get next part\n");
      ur_decoder_free(decoder);
      ur_encoder_free(encoder);
      free(cbor_data);
      psbt_free(psbt);
      free(original_psbt_bytes);
      return false;
    }

    // Feed to decoder
    ur_decoder_receive_part(decoder, part);
    free(part);
    parts_sent++;

    if (parts_sent % 10 == 0 || ur_decoder_is_complete(decoder)) {
      printf("  Parts sent: %d, Progress: %.1f%%\n", parts_sent,
             ur_decoder_estimated_percent_complete(decoder) * 100.0);
    }
  }

  bool success = false;

  if (!ur_decoder_is_complete(decoder)) {
    printf("❌ Decoder did not complete after %d parts\n", parts_sent);
  } else if (!ur_decoder_is_success(decoder)) {
    printf("❌ Decoder completed but not successful\n");
  } else {
    printf("✓ Decoder completed successfully\n");

    // Get decoded result
    ur_result_t *result = ur_decoder_get_result(decoder);
    if (result) {
      const uint8_t *decoded_cbor = result->cbor_data;
      size_t decoded_cbor_len = result->cbor_len;

      // Decode PSBT from CBOR
      psbt_data_t *decoded_psbt = psbt_from_cbor(decoded_cbor, decoded_cbor_len);

      if (decoded_psbt) {
        // Get PSBT data
        size_t decoded_psbt_len;
        const uint8_t *decoded_psbt_data =
            psbt_get_data(decoded_psbt, &decoded_psbt_len);

        // Compare with original bytes
        if (decoded_psbt_len == original_psbt_len &&
            memcmp(decoded_psbt_data, original_psbt_bytes, decoded_psbt_len) == 0) {
          printf("✅ PASS - Roundtrip successful, PSBT bytes match\n");
          success = true;
        } else {
          printf("❌ FAIL - PSBT bytes mismatch after roundtrip\n");
          printf("Original length: %zu, Decoded length: %zu\n",
                 original_psbt_len, decoded_psbt_len);

          // Show first bytes that differ
          if (decoded_psbt_len != original_psbt_len) {
            printf("Length mismatch!\n");
          } else {
            for (size_t i = 0; i < decoded_psbt_len; i++) {
              if (decoded_psbt_data[i] != original_psbt_bytes[i]) {
                printf("First mismatch at byte %zu: expected 0x%02x, got 0x%02x\n",
                       i, original_psbt_bytes[i], decoded_psbt_data[i]);
                break;
              }
            }
          }
        }

        psbt_free(decoded_psbt);
      } else {
        printf("❌ Failed to decode PSBT from roundtrip CBOR\n");
      }
    } else {
      printf("❌ Failed to get UR result\n");
    }
  }

  ur_decoder_free(decoder);
  ur_encoder_free(encoder);
  free(cbor_data);
  psbt_free(psbt);
  free(original_psbt_bytes);

  return success;
}

int main(int argc, char *argv[]) {
  printf("=== UR Encoder Test (PSBT) ===\n");

  const char *test_cases_dir = "tests/test_cases/PSBTs";

  // Check if a specific test file was provided
  if (argc > 1) {
    char filepath[512];
    const char *test_filename = argv[1];

    if (strstr(test_filename, test_cases_dir) == test_filename) {
      snprintf(filepath, sizeof(filepath), "%s", test_filename);
    } else {
      snprintf(filepath, sizeof(filepath), "%s/%s", test_cases_dir, test_filename);
    }

    printf("Running single test: %s\n", filepath);
    bool result = test_file(filepath);

    printf("\n=== Summary ===\n");
    printf("Test %s\n", result ? "PASSED" : "FAILED");

    return result ? 0 : 1;
  }

  // Run all tests
  DIR *dir = opendir(test_cases_dir);
  if (!dir) {
    fprintf(stderr, "Failed to open directory: %s\n", test_cases_dir);
    return 1;
  }

  struct dirent *entry;
  int total = 0;
  int passed = 0;

  // Collect test files
  char **test_files = (char **)malloc(100 * sizeof(char *));
  int file_count = 0;

  while ((entry = readdir(dir)) != NULL) {
    if (strstr(entry->d_name, ".psbt.bin")) {
      char filepath[512];
      snprintf(filepath, sizeof(filepath), "%s/%s", test_cases_dir,
               entry->d_name);
      test_files[file_count] = strdup(filepath);
      file_count++;
    }
  }
  closedir(dir);

  // Process each file
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
