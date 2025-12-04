#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Fragment reading utilities
char **read_fragments_from_file(const char *filepath, int *count);
void free_fragments(char **fragments, int count);

// Base64 encoding/decoding utilities
char *base64_encode(const uint8_t *data, size_t len);
bool base64_decode(const char *input, uint8_t **output, size_t *out_len);

// File reading utilities
uint8_t *read_binary_file(const char *filepath, size_t *out_len);
char *read_text_file_first_line(const char *filepath);

#endif // TEST_UTILS_H
