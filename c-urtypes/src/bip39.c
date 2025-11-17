#include "bip39.h"
#include "utils.h"
#include "cbor_encoder.h"
#include "cbor_decoder.h"
#include <string.h>

// BIP39 registry type (tag 301)
registry_type_t BIP39_TYPE = {
    .name = "crypto-bip39",
    .tag = CRYPTO_BIP39_TAG
};

// Create and destroy BIP39
bip39_data_t *bip39_new(char **words, size_t word_count, const char *lang) {
    if (!words || word_count == 0) return NULL;

    bip39_data_t *bip39 = safe_malloc(sizeof(bip39_data_t));
    if (!bip39) return NULL;

    // Copy words array
    bip39->words = safe_malloc(word_count * sizeof(char *));
    if (!bip39->words) {
        safe_free(bip39);
        return NULL;
    }

    for (size_t i = 0; i < word_count; i++) {
        bip39->words[i] = safe_strdup(words[i]);
        if (!bip39->words[i]) {
            // Free previously allocated words
            for (size_t j = 0; j < i; j++) {
                safe_free(bip39->words[j]);
            }
            safe_free(bip39->words);
            safe_free(bip39);
            return NULL;
        }
    }

    bip39->word_count = word_count;
    bip39->lang = lang ? safe_strdup(lang) : NULL;

    return bip39;
}

void bip39_free(bip39_data_t *bip39) {
    if (!bip39) return;

    if (bip39->words) {
        for (size_t i = 0; i < bip39->word_count; i++) {
            safe_free(bip39->words[i]);
        }
        safe_free(bip39->words);
    }

    safe_free(bip39->lang);
    safe_free(bip39);
}

static void bip39_item_free(registry_item_t *item) {
    if (!item) return;

    bip39_data_t *bip39 = (bip39_data_t *)item->data;
    bip39_free(bip39);
    safe_free(item);
}

// CBOR conversion functions
cbor_value_t *bip39_to_data_item(registry_item_t *item) {
    if (!item || !item->data) return NULL;

    bip39_data_t *bip39 = (bip39_data_t *)item->data;

    // Create map
    cbor_value_t *map = cbor_value_new_map();
    if (!map) return NULL;

    // Add words array (key 1)
    cbor_value_t *words_array = cbor_value_new_array();
    if (!words_array) {
        cbor_value_free(map);
        return NULL;
    }

    for (size_t i = 0; i < bip39->word_count; i++) {
        cbor_value_t *word = cbor_value_new_string(bip39->words[i]);
        if (!word || !cbor_array_append(words_array, word)) {
            if (word) cbor_value_free(word);
            cbor_value_free(words_array);
            cbor_value_free(map);
            return NULL;
        }
    }

    cbor_value_t *key1 = cbor_value_new_unsigned_int(1);
    if (!key1 || !cbor_map_set(map, key1, words_array)) {
        if (key1) cbor_value_free(key1);
        cbor_value_free(words_array);
        cbor_value_free(map);
        return NULL;
    }

    // Add lang (key 2) if present
    if (bip39->lang) {
        cbor_value_t *lang = cbor_value_new_string(bip39->lang);
        cbor_value_t *key2 = cbor_value_new_unsigned_int(2);

        if (!lang || !key2 || !cbor_map_set(map, key2, lang)) {
            if (lang) cbor_value_free(lang);
            if (key2) cbor_value_free(key2);
            cbor_value_free(map);
            return NULL;
        }
    }

    // Return plain map, NO TAG (matches Python implementation)
    return map;
}

registry_item_t *bip39_from_data_item(cbor_value_t *data_item) {
    if (!data_item) return NULL;

    // Expect plain map, NOT tagged (matches Python implementation)
    if (cbor_value_get_type(data_item) != CBOR_TYPE_MAP) return NULL;

    // Get words array (key 1)
    cbor_value_t *words_array = get_map_value(data_item, 1);
    if (!words_array || cbor_value_get_type(words_array) != CBOR_TYPE_ARRAY) return NULL;

    size_t word_count = cbor_value_get_array_size(words_array);
    if (word_count == 0) return NULL;

    char **words = safe_malloc(word_count * sizeof(char *));
    if (!words) return NULL;

    for (size_t i = 0; i < word_count; i++) {
        cbor_value_t *word_val = cbor_value_get_array_item(words_array, i);
        if (!word_val || cbor_value_get_type(word_val) != CBOR_TYPE_STRING) {
            for (size_t j = 0; j < i; j++) {
                safe_free(words[j]);
            }
            safe_free(words);
            return NULL;
        }

        const char *word_str = cbor_value_get_string(word_val);
        words[i] = safe_strdup(word_str);
        if (!words[i]) {
            for (size_t j = 0; j < i; j++) {
                safe_free(words[j]);
            }
            safe_free(words);
            return NULL;
        }
    }

    // Get lang (key 2) if present
    char *lang = NULL;
    cbor_value_t *lang_val = get_map_value(data_item, 2);
    if (lang_val && cbor_value_get_type(lang_val) == CBOR_TYPE_STRING) {
        const char *lang_str = cbor_value_get_string(lang_val);
        lang = safe_strdup(lang_str);
    }

    bip39_data_t *bip39 = bip39_new(words, word_count, lang);

    // Free temporary arrays
    for (size_t i = 0; i < word_count; i++) {
        safe_free(words[i]);
    }
    safe_free(words);
    safe_free(lang);

    if (!bip39) return NULL;

    return bip39_to_registry_item(bip39);
}

// Registry item interface for BIP39
registry_item_t *bip39_to_registry_item(bip39_data_t *bip39) {
    if (!bip39) return NULL;

    registry_item_t *item = safe_malloc(sizeof(registry_item_t));
    if (!item) return NULL;

    item->type = &BIP39_TYPE;
    item->data = bip39;
    item->to_data_item = bip39_to_data_item;
    item->from_data_item = bip39_from_data_item;
    item->free_item = bip39_item_free;

    return item;
}

bip39_data_t *bip39_from_registry_item(registry_item_t *item) {
    if (!item || item->type != &BIP39_TYPE) return NULL;
    return (bip39_data_t *)item->data;
}

// Convenience functions
uint8_t *bip39_to_cbor(bip39_data_t *bip39, size_t *out_len) {
    if (!bip39 || !out_len) return NULL;

    registry_item_t *item = bip39_to_registry_item(bip39);
    if (!item) return NULL;

    uint8_t *cbor_data = registry_item_to_cbor(item, out_len);

    // Free the registry item but not the bip39 data (it's still owned by caller)
    safe_free(item);

    return cbor_data;
}

bip39_data_t *bip39_from_cbor(const uint8_t *cbor_data, size_t len) {
    if (!cbor_data || len == 0) return NULL;

    registry_item_t *item = registry_item_from_cbor(cbor_data, len, bip39_from_data_item);
    if (!item) return NULL;

    bip39_data_t *bip39 = bip39_from_registry_item(item);

    // Transfer ownership of bip39 to caller and free the registry item wrapper
    item->data = NULL; // Don't free the bip39 data
    safe_free(item);

    return bip39;
}

// Accessors
char **bip39_get_words(bip39_data_t *bip39, size_t *out_count) {
    if (!bip39 || !out_count) return NULL;

    *out_count = bip39->word_count;
    return bip39->words;
}

const char *bip39_get_lang(bip39_data_t *bip39) {
    if (!bip39) return NULL;
    return bip39->lang;
}
