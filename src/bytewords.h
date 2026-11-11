#ifndef BYTEWORDS_H
#define BYTEWORDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ur_attributes.h"

/**
 * Encode bytes to bytewords minimal string (with CRC32)
 * @param data Data to encode
 * @param data_len Length of data
 * @param encoded Output encoded string (allocated)
 * @return true on success, false on error
 */
UR_WARN_UNUSED_RESULT bool bytewords_encode(const uint8_t *data,
                                            size_t data_len, char **encoded);

/**
 * Free bytewords result
 * @param ptr Pointer to free (can be char* or uint8_t*)
 */
void bytewords_free(void *ptr);

/**
 * Decode a bytewords minimal string, verifying and removing its CRC32
 * @param encoded Encoded bytewords string
 * @param decoded Output decoded bytes, without the CRC32 (allocated)
 * @param decoded_len Output decoded length
 * @return true on success; false on a malformed string or a CRC32 mismatch
 */
UR_WARN_UNUSED_RESULT bool bytewords_decode_raw(const char *encoded,
                                                uint8_t **decoded,
                                                size_t *decoded_len);

#endif // BYTEWORDS_H
