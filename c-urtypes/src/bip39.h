#ifndef URTYPES_BIP39_H
#define URTYPES_BIP39_H

#include <stdint.h>
#include <stddef.h>
#include "registry.h"

// BIP39 tag number
#define CRYPTO_BIP39_TAG 301

// BIP39 type structure
typedef struct {
    char **words;      // Array of word strings
    size_t word_count;
    char *lang;        // Language code (optional, can be NULL)
} bip39_data_t;

// BIP39 registry type
extern registry_type_t BIP39_TYPE;

// Create and destroy BIP39
bip39_data_t *bip39_new(char **words, size_t word_count, const char *lang);
void bip39_free(bip39_data_t *bip39);

// Registry item interface for BIP39
registry_item_t *bip39_to_registry_item(bip39_data_t *bip39);
bip39_data_t *bip39_from_registry_item(registry_item_t *item);

// CBOR conversion functions
cbor_value_t *bip39_to_data_item(registry_item_t *item);
registry_item_t *bip39_from_data_item(cbor_value_t *data_item);

// Convenience functions
uint8_t *bip39_to_cbor(bip39_data_t *bip39, size_t *out_len);
bip39_data_t *bip39_from_cbor(const uint8_t *cbor_data, size_t len);

// Accessors
char **bip39_get_words(bip39_data_t *bip39, size_t *out_count);
const char *bip39_get_lang(bip39_data_t *bip39);

#endif // URTYPES_BIP39_H
