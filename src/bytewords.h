#ifndef BYTEWORDS_H
#define BYTEWORDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Bytewords encoding styles
typedef enum {
  BYTEWORDS_STYLE_STANDARD = 0,
  BYTEWORDS_STYLE_URI,
  BYTEWORDS_STYLE_MINIMAL
} bytewords_style_t;

/**
 * Encode bytes to bytewords string (with CRC32)
 * @param style Bytewords style to use
 * @param data Data to encode
 * @param data_len Length of data
 * @param encoded Output encoded string (allocated)
 * @return true on success, false on error
 */
bool bytewords_encode(bytewords_style_t style, const uint8_t *data,
                      size_t data_len, char **encoded);

/**
 * Free bytewords result
 * @param ptr Pointer to free (can be char* or uint8_t*)
 */
void bytewords_free(void *ptr);

/**
 * Raw bytewords decode (no CRC32 validation) - for internal UR use
 * @param style Bytewords style to use
 * @param encoded Encoded bytewords string
 * @param decoded Output decoded bytes (allocated)
 * @param decoded_len Output decoded length
 * @return true on success, false on error
 */
bool bytewords_decode_raw(bytewords_style_t style, const char *encoded,
                          uint8_t **decoded, size_t *decoded_len);

#endif // BYTEWORDS_H