#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "../src/types/psbt.h"

// Test vector from Python urtypes test
void test_psbt_to_cbor(void) {
    TEST_CASE("PSBT to_cbor - Example/Test Vector");

    // PSBT data from Python test
    size_t psbt_data_len;
    uint8_t *psbt_data = hex_to_bytes(
        "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f000000000000000000",
        &psbt_data_len
    );
    ASSERT_NOT_NULL(psbt_data, "Failed to parse PSBT data hex");

    // Create PSBT
    psbt_data_t *psbt = psbt_new(psbt_data, psbt_data_len);
    ASSERT_NOT_NULL(psbt, "PSBT creation failed");

    // Expected CBOR (from Python test)
    // 58A770736274FF01009A020000000258E87A21B56DAF0C23BE8E7070456C336F7CBAA5C8757924F545887BB2ABDD750000000000FFFFFFFF838D0427D0EC650A68AA46BB0B098AEA4422C071B2CA78352A077959D07CEA1D0100000000FFFFFFFF0270AAF00800000000160014D85C2B71D0060B09C9886AEB815E50991DDA124D00E1F5050000000016001400AEA9A2E5F0F876A588DF5546E8742D1D87008F000000000000000000
    size_t expected_len;
    uint8_t *expected = hex_to_bytes(
        "58A770736274FF01009A020000000258E87A21B56DAF0C23BE8E7070456C336F7CBAA5C8757924F545887BB2ABDD750000000000FFFFFFFF838D0427D0EC650A68AA46BB0B098AEA4422C071B2CA78352A077959D07CEA1D0100000000FFFFFFFF0270AAF00800000000160014D85C2B71D0060B09C9886AEB815E50991DDA124D00E1F5050000000016001400AEA9A2E5F0F876A588DF5546E8742D1D87008F000000000000000000",
        &expected_len
    );
    ASSERT_NOT_NULL(expected, "Failed to parse expected hex");

    // Encode to CBOR
    size_t cbor_len;
    uint8_t *cbor = psbt_to_cbor(psbt, &cbor_len);
    ASSERT_NOT_NULL(cbor, "PSBT to_cbor failed");

    // Compare lengths
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
    free(expected);
    free(psbt_data);
    psbt_free(psbt);
}

void test_psbt_from_cbor(void) {
    TEST_CASE("PSBT from_cbor - Example/Test Vector");

    // CBOR data from Python test
    size_t cbor_len;
    uint8_t *cbor = hex_to_bytes(
        "58A770736274FF01009A020000000258E87A21B56DAF0C23BE8E7070456C336F7CBAA5C8757924F545887BB2ABDD750000000000FFFFFFFF838D0427D0EC650A68AA46BB0B098AEA4422C071B2CA78352A077959D07CEA1D0100000000FFFFFFFF0270AAF00800000000160014D85C2B71D0060B09C9886AEB815E50991DDA124D00E1F5050000000016001400AEA9A2E5F0F876A588DF5546E8742D1D87008F000000000000000000",
        &cbor_len
    );
    ASSERT_NOT_NULL(cbor, "Failed to parse CBOR hex");

    // Expected PSBT data
    size_t expected_len;
    uint8_t *expected_data = hex_to_bytes(
        "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f000000000000000000",
        &expected_len
    );
    ASSERT_NOT_NULL(expected_data, "Failed to parse expected data hex");

    // Decode from CBOR
    psbt_data_t *psbt = psbt_from_cbor(cbor, cbor_len);
    ASSERT_NOT_NULL(psbt, "PSBT from_cbor failed");

    // Verify data
    size_t data_len;
    const uint8_t *data = psbt_get_data(psbt, &data_len);
    ASSERT_NOT_NULL(data, "PSBT data is NULL");

    // Compare lengths
    ASSERT_EQUAL(data_len, expected_len, "PSBT data length mismatch");

    // Compare bytes
    if (data_len == expected_len) {
        ASSERT_BYTES_EQUAL(data, expected_data, data_len, "PSBT data mismatch");
    }

    // Debug output on failure
    if (memcmp(data, expected_data, data_len) != 0) {
        print_hex("Expected", expected_data, expected_len);
        print_hex("Got     ", data, data_len);
    }

    // Cleanup
    free(cbor);
    free(expected_data);
    psbt_free(psbt);
}

void test_psbt_roundtrip(void) {
    TEST_CASE("PSBT roundtrip - encode then decode");

    // Original PSBT data
    size_t orig_data_len;
    uint8_t *orig_data = hex_to_bytes(
        "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f000000000000000000",
        &orig_data_len
    );
    ASSERT_NOT_NULL(orig_data, "Failed to parse original data hex");

    // Create PSBT
    psbt_data_t *psbt_orig = psbt_new(orig_data, orig_data_len);
    ASSERT_NOT_NULL(psbt_orig, "PSBT creation failed");

    // Encode to CBOR
    size_t cbor_len;
    uint8_t *cbor = psbt_to_cbor(psbt_orig, &cbor_len);
    ASSERT_NOT_NULL(cbor, "PSBT to_cbor failed");

    // Decode from CBOR
    psbt_data_t *psbt_decoded = psbt_from_cbor(cbor, cbor_len);
    ASSERT_NOT_NULL(psbt_decoded, "PSBT from_cbor failed");

    // Verify data
    size_t decoded_len;
    const uint8_t *decoded_data = psbt_get_data(psbt_decoded, &decoded_len);
    ASSERT_NOT_NULL(decoded_data, "Decoded PSBT data is NULL");

    // Compare lengths
    ASSERT_EQUAL(decoded_len, orig_data_len, "Decoded PSBT data length mismatch");

    // Compare bytes
    if (decoded_len == orig_data_len) {
        ASSERT_BYTES_EQUAL(decoded_data, orig_data, decoded_len, "Decoded PSBT data mismatch");
    }

    // Debug output on failure
    if (memcmp(decoded_data, orig_data, decoded_len) != 0) {
        print_hex("Expected", orig_data, orig_data_len);
        print_hex("Got     ", decoded_data, decoded_len);
    }

    // Cleanup
    free(cbor);
    free(orig_data);
    psbt_free(psbt_orig);
    psbt_free(psbt_decoded);
}

int main(void) {
    TEST_SUITE_START("PSBT Tests");

    test_psbt_to_cbor();
    test_psbt_from_cbor();
    test_psbt_roundtrip();

    TEST_SUITE_END();

    return tests_failed > 0 ? 1 : 0;
}
