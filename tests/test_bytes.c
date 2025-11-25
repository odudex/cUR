#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "../src/types/bytes_type.h"

void test_bytes_creation(void) {
    TEST_CASE("Bytes creation and data access");

    const char *test_str = "Hello, World!";
    size_t test_len = strlen(test_str);

    // Create Bytes
    bytes_data_t *bytes = bytes_new((const uint8_t *)test_str, test_len);
    ASSERT_NOT_NULL(bytes, "Bytes creation failed");

    // Get data
    size_t data_len;
    const uint8_t *data = bytes_get_data(bytes, &data_len);
    ASSERT_NOT_NULL(data, "Bytes data is NULL");

    // Compare length
    ASSERT_EQUAL(data_len, test_len, "Data length mismatch");

    // Compare data
    ASSERT_BYTES_EQUAL(data, (const uint8_t *)test_str, data_len, "Data content mismatch");

    // Cleanup
    bytes_free(bytes);
}

void test_bytes_to_cbor(void) {
    TEST_CASE("Bytes to_cbor - Simple test");

    // Test data
    const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t test_len = sizeof(test_data);

    // Create Bytes
    bytes_data_t *bytes = bytes_new(test_data, test_len);
    ASSERT_NOT_NULL(bytes, "Bytes creation failed");

    // Encode to CBOR
    size_t cbor_len;
    uint8_t *cbor = bytes_to_cbor(bytes, &cbor_len);
    ASSERT_NOT_NULL(cbor, "Bytes to_cbor failed");

    // CBOR format for byte string (major type 2):
    // For 5 bytes: 0x45 (major type 2, length 5) followed by the bytes
    // Expected: 45 01 02 03 04 05
    uint8_t expected[] = {0x45, 0x01, 0x02, 0x03, 0x04, 0x05};
    size_t expected_len = sizeof(expected);

    // Compare length
    ASSERT_EQUAL(cbor_len, expected_len, "CBOR length mismatch");

    // Compare bytes
    if (cbor_len == expected_len) {
        ASSERT_BYTES_EQUAL(cbor, expected, cbor_len, "CBOR bytes mismatch");
    }

    // Debug output on failure
    if (memcmp(cbor, expected, cbor_len) != 0) {
        print_hex("Expected", expected, expected_len);
        print_hex("Got     ", cbor, cbor_len);
    }

    // Cleanup
    free(cbor);
    bytes_free(bytes);
}

void test_bytes_from_cbor(void) {
    TEST_CASE("Bytes from_cbor - Simple test");

    // CBOR data: byte string with 5 bytes (0x45 = major type 2, length 5)
    uint8_t cbor_data[] = {0x45, 0x01, 0x02, 0x03, 0x04, 0x05};
    size_t cbor_len = sizeof(cbor_data);

    // Expected data
    uint8_t expected[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t expected_len = sizeof(expected);

    // Decode from CBOR
    bytes_data_t *bytes = bytes_from_cbor(cbor_data, cbor_len);
    ASSERT_NOT_NULL(bytes, "Bytes from_cbor failed");

    // Get data
    size_t data_len;
    const uint8_t *data = bytes_get_data(bytes, &data_len);
    ASSERT_NOT_NULL(data, "Bytes data is NULL");

    // Compare length
    ASSERT_EQUAL(data_len, expected_len, "Data length mismatch");

    // Compare bytes
    if (data_len == expected_len) {
        ASSERT_BYTES_EQUAL(data, expected, data_len, "Data bytes mismatch");
    }

    // Debug output on failure
    if (memcmp(data, expected, data_len) != 0) {
        print_hex("Expected", expected, expected_len);
        print_hex("Got     ", data, data_len);
    }

    // Cleanup
    bytes_free(bytes);
}

void test_bytes_roundtrip(void) {
    TEST_CASE("Bytes roundtrip - encode then decode");

    // Original data
    const uint8_t orig_data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    size_t orig_len = sizeof(orig_data);

    // Create Bytes
    bytes_data_t *bytes_orig = bytes_new(orig_data, orig_len);
    ASSERT_NOT_NULL(bytes_orig, "Bytes creation failed");

    // Encode to CBOR
    size_t cbor_len;
    uint8_t *cbor = bytes_to_cbor(bytes_orig, &cbor_len);
    ASSERT_NOT_NULL(cbor, "Bytes to_cbor failed");

    // Decode from CBOR
    bytes_data_t *bytes_decoded = bytes_from_cbor(cbor, cbor_len);
    ASSERT_NOT_NULL(bytes_decoded, "Bytes from_cbor failed");

    // Get decoded data
    size_t decoded_len;
    const uint8_t *decoded_data = bytes_get_data(bytes_decoded, &decoded_len);
    ASSERT_NOT_NULL(decoded_data, "Decoded data is NULL");

    // Compare length
    ASSERT_EQUAL(decoded_len, orig_len, "Decoded data length mismatch");

    // Compare bytes
    if (decoded_len == orig_len) {
        ASSERT_BYTES_EQUAL(decoded_data, orig_data, decoded_len, "Decoded data mismatch");
    }

    // Debug output on failure
    if (memcmp(decoded_data, orig_data, decoded_len) != 0) {
        print_hex("Expected", orig_data, orig_len);
        print_hex("Got     ", decoded_data, decoded_len);
    }

    // Cleanup
    free(cbor);
    bytes_free(bytes_orig);
    bytes_free(bytes_decoded);
}

void test_bytes_empty(void) {
    TEST_CASE("Bytes with empty data");

    // Create empty Bytes
    bytes_data_t *bytes = bytes_new(NULL, 0);
    ASSERT_NOT_NULL(bytes, "Empty Bytes creation failed");

    // Get data
    size_t data_len;
    const uint8_t *data = bytes_get_data(bytes, &data_len);

    // Length should be 0
    ASSERT_EQUAL(data_len, 0, "Empty Bytes length should be 0");

    // Encode to CBOR
    size_t cbor_len;
    uint8_t *cbor = bytes_to_cbor(bytes, &cbor_len);
    ASSERT_NOT_NULL(cbor, "Empty Bytes to_cbor failed");

    // CBOR for empty byte string: 0x40 (major type 2, length 0)
    uint8_t expected[] = {0x40};
    ASSERT_EQUAL(cbor_len, 1, "Empty Bytes CBOR length should be 1");
    if (cbor_len == 1) {
        ASSERT_BYTES_EQUAL(cbor, expected, 1, "Empty Bytes CBOR mismatch");
    }

    // Cleanup
    free(cbor);
    bytes_free(bytes);
}

int main(void) {
    TEST_SUITE_START("Bytes Tests");

    test_bytes_creation();
    test_bytes_to_cbor();
    test_bytes_from_cbor();
    test_bytes_roundtrip();
    test_bytes_empty();

    TEST_SUITE_END();

    return tests_failed > 0 ? 1 : 0;
}
