#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "../src/types/bip39.h"

// Test vector from Python urtypes test
void test_bip39_from_cbor(void) {
    TEST_CASE("BIP39 from_cbor - Example/Test Vector (16 byte (128-bit) seed, encoded as BIP39)");

    // CBOR data from Python test
    size_t cbor_len;
    uint8_t *cbor = hex_to_bytes(
        "a2018c66736869656c646567726f75706565726f6465656177616b65646c6f636b6773617573616765646361736865676c6172656477617665646372657765666c616d6565676c6f76650262656e",
        &cbor_len
    );
    ASSERT_NOT_NULL(cbor, "Failed to parse CBOR hex");

    // Decode from CBOR
    bip39_data_t *bip39 = bip39_from_cbor(cbor, cbor_len);
    ASSERT_NOT_NULL(bip39, "BIP39 from_cbor failed");

    // Verify word count
    size_t word_count;
    char **words = bip39_get_words(bip39, &word_count);
    ASSERT_EQUAL(word_count, 12, "Word count mismatch");

    // Verify words
    const char *expected_words[] = {
        "shield", "group", "erode", "awake", "lock", "sausage",
        "cash", "glare", "wave", "crew", "flame", "glove"
    };

    for (size_t i = 0; i < word_count && i < 12; i++) {
        ASSERT_STRING_EQUAL(words[i], expected_words[i], "Word mismatch");
    }

    // Cleanup
    free(cbor);
    bip39_free(bip39);
}

void test_bip39_from_cbor_no_lang(void) {
    TEST_CASE("BIP39 from_cbor - Without language field");

    // CBOR data without language field (just words array)
    // This is a simplified CBOR with just the words array (key 1)
    size_t cbor_len;
    uint8_t *cbor = hex_to_bytes(
        "a1018c66736869656c646567726f75706565726f6465656177616b65646c6f636b6773617573616765646361736865676c6172656477617665646372657765666c616d6565676c6f7665",
        &cbor_len
    );
    ASSERT_NOT_NULL(cbor, "Failed to parse CBOR hex");

    // Decode from CBOR
    bip39_data_t *bip39 = bip39_from_cbor(cbor, cbor_len);
    ASSERT_NOT_NULL(bip39, "BIP39 from_cbor failed");

    // Verify word count
    size_t word_count;
    char **words = bip39_get_words(bip39, &word_count);
    ASSERT_EQUAL(word_count, 12, "Word count mismatch");

    // Verify words
    const char *expected_words[] = {
        "shield", "group", "erode", "awake", "lock", "sausage",
        "cash", "glare", "wave", "crew", "flame", "glove"
    };

    for (size_t i = 0; i < word_count && i < 12; i++) {
        ASSERT_STRING_EQUAL(words[i], expected_words[i], "Word mismatch");
    }

    // Cleanup
    free(cbor);
    bip39_free(bip39);
}

int main(void) {
    TEST_SUITE_START("BIP39 Tests");

    test_bip39_from_cbor();
    test_bip39_from_cbor_no_lang();

    TEST_SUITE_END();

    return tests_failed > 0 ? 1 : 0;
}
