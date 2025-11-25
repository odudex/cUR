#include "ec_key.h"
#include "utils.h"
#include "cbor_decoder.h"
#include <string.h>
#include <stdio.h>

// ECKey registry type (tag 306)
registry_type_t ECKEY_TYPE = {
    .name = "crypto-eckey",
    .tag = CRYPTO_ECKEY_TAG
};

// Create and destroy ECKey
ec_key_data_t *ec_key_new(const uint8_t *data, size_t data_len, int curve, bool has_private_key) {
    if (!data || data_len == 0) return NULL;

    ec_key_data_t *ec_key = safe_malloc(sizeof(ec_key_data_t));
    if (!ec_key) return NULL;

    ec_key->data = safe_malloc(data_len);
    if (!ec_key->data) {
        safe_free(ec_key);
        return NULL;
    }

    memcpy(ec_key->data, data, data_len);
    ec_key->data_len = data_len;
    ec_key->curve = curve;
    ec_key->has_private_key = has_private_key;

    return ec_key;
}

void ec_key_free(ec_key_data_t *ec_key) {
    if (!ec_key) return;
    safe_free(ec_key->data);
    safe_free(ec_key);
}

static void ec_key_item_free(registry_item_t *item) {
    if (!item) return;
    ec_key_data_t *ec_key = (ec_key_data_t *)item->data;
    ec_key_free(ec_key);
    safe_free(item);
}

// Parse ECKey from CBOR data item
registry_item_t *ec_key_from_data_item(cbor_value_t *data_item) {
    if (!data_item) return NULL;

    // ECKey is a map
    if (cbor_value_get_type(data_item) != CBOR_TYPE_MAP) return NULL;

    // Get key data (key 3, required)
    cbor_value_t *data_val = get_map_value(data_item, 3);
    if (!data_val || cbor_value_get_type(data_val) != CBOR_TYPE_BYTES) {
        return NULL;
    }

    size_t data_len;
    const uint8_t *data = cbor_value_get_bytes(data_val, &data_len);
    if (!data || data_len == 0) return NULL;

    // Get curve (key 1, optional)
    int curve = -1;
    cbor_value_t *curve_val = get_map_value(data_item, 1);
    if (curve_val && cbor_value_get_type(curve_val) == CBOR_TYPE_UNSIGNED_INT) {
        curve = (int)cbor_value_get_uint(curve_val);
    }

    // Get private_key flag (key 2, optional)
    bool has_private_key = false;
    cbor_value_t *priv_val = get_map_value(data_item, 2);
    if (priv_val) {
        has_private_key = true;
    }

    ec_key_data_t *ec_key = ec_key_new(data, data_len, curve, has_private_key);
    if (!ec_key) return NULL;

    return ec_key_to_registry_item(ec_key);
}

// Registry item interface
registry_item_t *ec_key_to_registry_item(ec_key_data_t *ec_key) {
    if (!ec_key) return NULL;

    registry_item_t *item = safe_malloc(sizeof(registry_item_t));
    if (!item) return NULL;

    item->type = &ECKEY_TYPE;
    item->data = ec_key;
    item->to_data_item = NULL;  // Not needed for read-only
    item->from_data_item = ec_key_from_data_item;
    item->free_item = ec_key_item_free;

    return item;
}

ec_key_data_t *ec_key_from_registry_item(registry_item_t *item) {
    if (!item || item->type != &ECKEY_TYPE) return NULL;
    return (ec_key_data_t *)item->data;
}

ec_key_data_t *ec_key_from_cbor(const uint8_t *cbor_data, size_t len) {
    if (!cbor_data || len == 0) return NULL;

    registry_item_t *item = registry_item_from_cbor(cbor_data, len, ec_key_from_data_item);
    if (!item) return NULL;

    ec_key_data_t *ec_key = ec_key_from_registry_item(item);

    // Transfer ownership
    item->data = NULL;
    safe_free(item);

    return ec_key;
}

// Generate descriptor key string (hex encoding)
char *ec_key_descriptor_key(ec_key_data_t *ec_key) {
    if (!ec_key || !ec_key->data) return NULL;

    // Allocate buffer for hex string (2 chars per byte + null terminator)
    char *hex = safe_malloc(ec_key->data_len * 2 + 1);
    if (!hex) return NULL;

    for (size_t i = 0; i < ec_key->data_len; i++) {
        sprintf(hex + i * 2, "%02x", ec_key->data[i]);
    }
    hex[ec_key->data_len * 2] = '\0';

    return hex;
}
