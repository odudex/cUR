#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "../src/bip39.h"

// Test vector from Python urtypes test
void test_bip39_to_cbor(void) {
    TEST_CASE("BIP39 to_cbor - Example/Test Vector (16 byte (128-bit) seed, encoded as BIP39)");

    // Create BIP39 with test words
    char *words[] = {
        "shield", "group", "erode", "awake", "lock", "sausage",
        "cash", "glare", "wave", "crew", "flame", "glove"
    };
    size_t word_count = 12;

    bip39_data_t *bip39 = bip39_new(words, word_count, "en");
    ASSERT_NOT_NULL(bip39, "BIP39 creation failed");

    // Expected CBOR (from Python test)
    // a2018c66736869656c646567726f75706565726f6465656177616b65646c6f636b6773617573616765646361736865676c6172656477617665646372657765666c616d6565676c6f76650262656e
    size_t expected_len;
    uint8_t *expected = hex_to_bytes(
        "a2018c66736869656c646567726f75706565726f6465656177616b65646c6f636b6773617573616765646361736865676c6172656477617665646372657765666c616d6565676c6f76650262656e",
        &expected_len
    );
    ASSERT_NOT_NULL(expected, "Failed to parse expected hex");

    // Encode to CBOR
    size_t cbor_len;
    uint8_t *cbor = bip39_to_cbor(bip39, &cbor_len);
    ASSERT_NOT_NULL(cbor, "BIP39 to_cbor failed");

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
    bip39_free(bip39);
}

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

    // Verify language
    const char *lang = bip39_get_lang(bip39);
    ASSERT_NOT_NULL(lang, "Language is NULL");
    if (lang) {
        ASSERT_STRING_EQUAL(lang, "en", "Language mismatch");
    }

    // Cleanup
    free(cbor);
    bip39_free(bip39);
}

void test_bip39_roundtrip(void) {
    TEST_CASE("BIP39 roundtrip - encode then decode");

    // Create original BIP39
    char *words[] = {
        "shield", "group", "erode", "awake", "lock", "sausage",
        "cash", "glare", "wave", "crew", "flame", "glove"
    };
    size_t word_count = 12;

    bip39_data_t *bip39_orig = bip39_new(words, word_count, "en");
    ASSERT_NOT_NULL(bip39_orig, "BIP39 creation failed");

    // Encode to CBOR
    size_t cbor_len;
    uint8_t *cbor = bip39_to_cbor(bip39_orig, &cbor_len);
    ASSERT_NOT_NULL(cbor, "BIP39 to_cbor failed");

    // Decode from CBOR
    bip39_data_t *bip39_decoded = bip39_from_cbor(cbor, cbor_len);
    ASSERT_NOT_NULL(bip39_decoded, "BIP39 from_cbor failed");

    // Verify word count
    size_t decoded_word_count;
    char **decoded_words = bip39_get_words(bip39_decoded, &decoded_word_count);
    ASSERT_EQUAL(decoded_word_count, word_count, "Decoded word count mismatch");

    // Verify words
    for (size_t i = 0; i < word_count; i++) {
        ASSERT_STRING_EQUAL(decoded_words[i], words[i], "Decoded word mismatch");
    }

    // Verify language
    const char *decoded_lang = bip39_get_lang(bip39_decoded);
    ASSERT_NOT_NULL(decoded_lang, "Decoded language is NULL");
    if (decoded_lang) {
        ASSERT_STRING_EQUAL(decoded_lang, "en", "Decoded language mismatch");
    }

    // Cleanup
    free(cbor);
    bip39_free(bip39_orig);
    bip39_free(bip39_decoded);
}

int main(void) {
    TEST_SUITE_START("BIP39 Tests");

    test_bip39_to_cbor();
    test_bip39_from_cbor();
    test_bip39_roundtrip();

    TEST_SUITE_END();

    return tests_failed > 0 ? 1 : 0;
}
