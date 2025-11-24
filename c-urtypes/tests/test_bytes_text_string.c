#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/bytes_type.h"
#include "../src/cbor_decoder.h"

int main() {
    printf("Testing bytes type with CBOR text string (type 3)...\n\n");

    // CBOR data from the user - text string containing output descriptor
    // Format: 0x79 (text string, 2-byte length) 0x01 0x3e (length=318) followed by the string
    const uint8_t cbor_data[] = {
        0x79, 0x01, 0x3e, // text string, length 318
        'w', 's', 'h', '(', 'a', 'n', 'd', '_', 'v', '(', 'v', ':', '0', ',', 'a', 'n', 'd', '_', 'v', '(',
        'v', ':', 'p', 'k', '(', '[', '4', '0', '2', '5', '9', 'a', 'b', '7', '/', '4', '8', 'h', '/', '1',
        'h', '/', '0', 'h', '/', '2', 'h', ']', 't', 'p', 'u', 'b', 'D', 'F', 'c', 'Y', '6', 'n', 'T', 'i',
        'M', 'A', 'W', 'B', 'd', '5', 'd', '2', 'b', 'S', '8', 'J', 'Z', 'v', 'j', 'c', 'a', 'L', 'j', 'C',
        '6', 'G', 'E', '6', 'X', 'n', 'P', 'A', 'J', 'A', 'P', 'U', 'k', 'V', 'j', '5', 'w', 'a', '5', 'P',
        'y', 'b', '4', 'g', 'u', 'm', 'x', '1', 'Z', 'W', 'v', 'n', 'X', 'Q', '8', 't', 'm', 'o', 'r', 'C',
        'm', 'p', 'A', 'y', 'a', 'i', '6', '9', 'K', '9', 'h', 'D', '2', 'm', 'G', 'Q', 'U', 'e', 'N', 'k',
        'u', 'X', 'f', 'j', 'z', 't', 's', 'f', 'q', 'n', 'E', '5', 'F', 'M', 'k', '1', 'C', 'C', 'h', '/',
        '<', '0', ';', '1', '>', '/', '*', ')', ',', 'p', 'k', '(', '[', '4', '0', '2', '5', '9', 'a', 'b',
        '7', '/', '4', '8', 'h', '/', '1', 'h', '/', '0', 'h', '/', '2', 'h', ']', 't', 'p', 'u', 'b', 'D',
        'F', 'c', 'Y', '6', 'n', 'T', 'i', 'M', 'A', 'W', 'B', 'd', '5', 'd', '2', 'b', 'S', '8', 'J', 'Z',
        'v', 'j', 'c', 'a', 'L', 'j', 'C', '6', 'G', 'E', '6', 'X', 'n', 'P', 'A', 'J', 'A', 'P', 'U', 'k',
        'V', 'j', '5', 'w', 'a', '5', 'P', 'y', 'b', '4', 'g', 'u', 'm', 'x', '1', 'Z', 'W', 'v', 'n', 'X',
        'Q', '8', 't', 'm', 'o', 'r', 'C', 'm', 'p', 'A', 'y', 'a', 'i', '6', '9', 'K', '9', 'h', 'D', '2',
        'm', 'G', 'Q', 'U', 'e', 'N', 'k', 'u', 'X', 'f', 'j', 'z', 't', 's', 'f', 'q', 'n', 'E', '5', 'F',
        'M', 'k', '1', 'C', 'C', 'h', '/', '<', '0', ';', '1', '>', '/', '*', ')', ')', ')', ')', ')'
    };

    const char *expected_string =
        "wsh(and_v(v:0,and_v(v:pk([40259ab7/48h/1h/0h/2h]tpubDFcY6nTiMAWBd5d2bS8JZvjcaLjC6GE6XnPAJAPUkVj5wa5Pyb4gumx1ZWvnXQ8tmorCmpAyai69K9hD2mGQUeNkuXfjztsfqnE5FMk1CCh/<0;1>/*),pk([40259ab7/48h/1h/0h/2h]tpubDFcY6nTiMAWBd5d2bS8JZvjcaLjC6GE6XnPAJAPUkVj5wa5Pyb4gumx1ZWvnXQ8tmorCmpAyai69K9hD2mGQUeNkuXfjztsfqnE5FMk1CCh/<0;1>/*))))";

    printf("Input CBOR data: %zu bytes\n", sizeof(cbor_data));
    printf("Expected string length: %zu\n", strlen(expected_string));
    printf("Expected string: %s\n\n", expected_string);

    // Step 1: Decode CBOR to cbor_value_t
    cbor_decoder_t *decoder = cbor_decoder_new(cbor_data, sizeof(cbor_data));
    assert(decoder != NULL);

    cbor_value_t *value = cbor_decoder_decode(decoder);
    assert(value != NULL);

    cbor_type_t type = cbor_value_get_type(value);
    printf("✓ CBOR decoded successfully\n");
    printf("  CBOR type: %d (expected CBOR_TYPE_STRING = %d)\n", type, CBOR_TYPE_STRING);
    assert(type == CBOR_TYPE_STRING);

    const char *decoded_string = cbor_value_get_string(value);
    assert(decoded_string != NULL);
    printf("✓ String extracted from CBOR: '%s'\n", decoded_string);
    assert(strcmp(decoded_string, expected_string) == 0);
    printf("✓ String matches expected value\n\n");

    // Step 2: Convert to bytes using bytes_from_data_item
    registry_item_t *item = bytes_from_data_item(value);
    assert(item != NULL);
    printf("✓ bytes_from_data_item accepted CBOR text string (type 3)\n");

    bytes_data_t *bytes = bytes_from_registry_item(item);
    assert(bytes != NULL);

    // Step 3: Verify the bytes data
    size_t len;
    const uint8_t *data = bytes_get_data(bytes, &len);
    assert(data != NULL);
    assert(len == strlen(expected_string));
    printf("✓ Bytes length: %zu (expected %zu)\n", len, strlen(expected_string));

    assert(memcmp(data, expected_string, len) == 0);
    printf("✓ Bytes data matches expected string\n");

    // Print the data as a string to verify
    printf("\nExtracted bytes as string: '%.*s'\n", (int)len, data);

    // Cleanup
    cbor_value_free(value);
    cbor_decoder_free(decoder);
    safe_free(item);
    bytes_free(bytes);

    printf("\n✅ All tests passed! CBOR text string (type 3) processed correctly.\n");

    return 0;
}
