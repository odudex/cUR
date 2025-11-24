#ifndef URTYPES_EC_KEY_H
#define URTYPES_EC_KEY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "registry.h"

// ECKey tag number
#define CRYPTO_ECKEY_TAG 306

// ECKey type structure
typedef struct {
    uint8_t *data;        // Key data (public key bytes)
    size_t data_len;
    int curve;            // -1 means not set
    bool has_private_key; // Whether private key field is present
} ec_key_data_t;

// ECKey registry type
extern registry_type_t ECKEY_TYPE;

// Create and destroy ECKey
ec_key_data_t *ec_key_new(const uint8_t *data, size_t data_len, int curve, bool has_private_key);
void ec_key_free(ec_key_data_t *ec_key);

// Registry item interface for ECKey
registry_item_t *ec_key_to_registry_item(ec_key_data_t *ec_key);
ec_key_data_t *ec_key_from_registry_item(registry_item_t *item);

// CBOR conversion functions (read-only)
registry_item_t *ec_key_from_data_item(cbor_value_t *data_item);
ec_key_data_t *ec_key_from_cbor(const uint8_t *cbor_data, size_t len);

// Generate descriptor key string (hex encoding)
char *ec_key_descriptor_key(ec_key_data_t *ec_key);

#endif // URTYPES_EC_KEY_H
