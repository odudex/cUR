#include "hd_key.h"
#include "../ur_attributes.h"
#include "byte_buffer.h"
#include "cbor_decoder.h"
#include <stdio.h>
#include <string.h>

// HDKey registry type (tag 303)
registry_type_t HDKEY_TYPE = {.name = "crypto-hdkey", .tag = CRYPTO_HDKEY_TAG};

// Create and destroy HDKey
hd_key_data_t *hd_key_new(void) {
  hd_key_data_t *hd_key = safe_malloc(sizeof(hd_key_data_t));
  if (!hd_key)
    return NULL;

  hd_key->master = false;
  hd_key->key = NULL;
  hd_key->key_len = 0;
  hd_key->chain_code = NULL;
  hd_key->private_key = NULL;
  hd_key->private_key_len = 0;
  hd_key->origin = NULL;
  hd_key->children = NULL;
  hd_key->parent_fingerprint = NULL;

  return hd_key;
}

void hd_key_free(hd_key_data_t *hd_key) {
  if (!hd_key)
    return;

  free(hd_key->key);
  free(hd_key->chain_code);
  free(hd_key->private_key);
  free(hd_key->parent_fingerprint);

  if (hd_key->origin)
    keypath_free(hd_key->origin);
  if (hd_key->children)
    keypath_free(hd_key->children);

  free(hd_key);
}

// Read an optional keypath field (tag 304 optional). *out stays NULL when the
// field is absent; a present field that does not parse fails the key.
static UR_WARN_UNUSED_RESULT bool read_keypath_field(cbor_value_t *map, int key,
                                                     keypath_data_t **out) {
  cbor_value_t *val = get_map_value(map, key);
  if (!val)
    return true;
  if (cbor_value_get_type(val) == CBOR_TYPE_TAG)
    val = cbor_value_get_tag_content(val);
  registry_item_t *item = keypath_from_data_item(val);
  if (!item)
    return false;
  *out = keypath_from_registry_item(item);
  free(item);
  return *out != NULL;
}

// Parse HDKey from CBOR data item. A field that is present but malformed
// fails the whole key: dropping it would describe a different key.
registry_item_t *hd_key_from_data_item(cbor_value_t *data_item) {
  if (!data_item)
    return NULL;

  // HDKey is a map
  if (cbor_value_get_type(data_item) != CBOR_TYPE_MAP)
    return NULL;

  hd_key_data_t *hd_key = hd_key_new();
  if (!hd_key)
    return NULL;

  // Check if master key (key 1)
  cbor_value_t *master_val = get_map_value(data_item, 1);
  if (master_val) {
    if (cbor_value_get_type(master_val) != CBOR_TYPE_BOOL)
      goto fail;
    hd_key->master = cbor_value_get_bool(master_val);
  }

  // Key 2 is is-private (bool) in BCR-2020-007; the byte-string form is this
  // library's legacy private-key field. Private keys are not accepted.
  cbor_value_t *priv_val = get_map_value(data_item, 2);
  if (priv_val) {
    cbor_type_t priv_type = cbor_value_get_type(priv_val);
    if (priv_type == CBOR_TYPE_BOOL) {
      if (cbor_value_get_bool(priv_val))
        goto fail;
    } else if (priv_type == CBOR_TYPE_BYTES) {
      const uint8_t *priv_data =
          cbor_value_get_bytes(priv_val, &hd_key->private_key_len);
      if (priv_data && hd_key->private_key_len > 0) {
        hd_key->private_key = safe_malloc(hd_key->private_key_len);
        if (!hd_key->private_key)
          goto fail;
        memcpy(hd_key->private_key, priv_data, hd_key->private_key_len);
      }
    } else {
      goto fail;
    }
  }

  // Get key (key 3, required)
  cbor_value_t *key_val = get_map_value(data_item, 3);
  if (!key_val || cbor_value_get_type(key_val) != CBOR_TYPE_BYTES)
    goto fail;
  const uint8_t *key_data = cbor_value_get_bytes(key_val, &hd_key->key_len);
  if (!key_data || hd_key->key_len == 0)
    goto fail;
  hd_key->key = safe_malloc(hd_key->key_len);
  if (!hd_key->key)
    goto fail;
  memcpy(hd_key->key, key_data, hd_key->key_len);

  // Get chain code (key 4, optional)
  cbor_value_t *chain_val = get_map_value(data_item, 4);
  if (chain_val) {
    size_t chain_len = 0;
    const uint8_t *chain_data =
        cbor_value_get_type(chain_val) == CBOR_TYPE_BYTES
            ? cbor_value_get_bytes(chain_val, &chain_len)
            : NULL;
    if (!chain_data || chain_len != 32)
      goto fail;
    hd_key->chain_code = safe_malloc(32);
    if (!hd_key->chain_code)
      goto fail;
    memcpy(hd_key->chain_code, chain_data, 32);
  }

  // Skip use_info (key 5) - not needed for descriptors

  // Origin (key 6) and children (key 7), both optional
  if (!read_keypath_field(data_item, 6, &hd_key->origin) ||
      !read_keypath_field(data_item, 7, &hd_key->children))
    goto fail;

  // Get parent fingerprint (key 8, optional)
  cbor_value_t *pfp_val = get_map_value(data_item, 8);
  if (pfp_val) {
    if (cbor_value_get_type(pfp_val) != CBOR_TYPE_UNSIGNED_INT ||
        cbor_value_get_uint(pfp_val) > UINT32_MAX)
      goto fail;
    uint32_t fp_int = (uint32_t)cbor_value_get_uint(pfp_val);
    hd_key->parent_fingerprint = safe_malloc(4);
    if (!hd_key->parent_fingerprint)
      goto fail;
    // Big-endian encoding
    hd_key->parent_fingerprint[0] = (fp_int >> 24) & 0xFF;
    hd_key->parent_fingerprint[1] = (fp_int >> 16) & 0xFF;
    hd_key->parent_fingerprint[2] = (fp_int >> 8) & 0xFF;
    hd_key->parent_fingerprint[3] = fp_int & 0xFF;
  }

  // Skip name (key 9) and note (key 10) - not needed for descriptors

  registry_item_t *item = hd_key_to_registry_item(hd_key);
  if (!item)
    goto fail;
  return item;

fail:
  hd_key_free(hd_key);
  return NULL;
}

// Set map[key] = value. Frees `value` if the key allocation or the insert
// fails, so the caller can pass a freshly built value inline. cbor_map_set()
// takes ownership of both only on success.
static UR_WARN_UNUSED_RESULT bool map_set_owned(cbor_value_t *map, uint64_t key,
                                                cbor_value_t *value) {
  cbor_value_t *k = cbor_value_new_unsigned_int(key);
  if (!k || !value || !cbor_map_set(map, k, value)) {
    cbor_value_free(k);
    cbor_value_free(value);
    return false;
  }
  return true;
}

// Wrap `content` in a tag and store it. Takes ownership of `content`.
static UR_WARN_UNUSED_RESULT bool map_set_tagged(cbor_value_t *map,
                                                 uint64_t key, uint64_t tag,
                                                 cbor_value_t *content) {
  if (!content)
    return false;
  cbor_value_t *tagged = cbor_value_new_tag(tag, content);
  if (!tagged) {
    cbor_value_free(content);
    return false;
  }
  return map_set_owned(map, key, tagged);
}

cbor_value_t *hd_key_to_data_item(hd_key_data_t *hd_key) {
  if (!hd_key)
    return NULL;

  cbor_value_t *map = cbor_value_new_map();
  if (!map)
    return NULL;

  // Every field is mandatory once present: a key emitted without its origin
  // keypath, chain code or parent fingerprint is a different key, so a failed
  // insert abandons the whole item rather than shipping a partial one.
  if (hd_key->master && !map_set_owned(map, 1, cbor_value_new_bool(true)))
    goto fail;

  if (hd_key->private_key && hd_key->private_key_len > 0 &&
      !map_set_owned(
          map, 2,
          cbor_value_new_bytes(hd_key->private_key, hd_key->private_key_len)))
    goto fail;

  if (hd_key->key && hd_key->key_len > 0 &&
      !map_set_owned(map, 3,
                     cbor_value_new_bytes(hd_key->key, hd_key->key_len)))
    goto fail;

  if (hd_key->chain_code &&
      !map_set_owned(map, 4, cbor_value_new_bytes(hd_key->chain_code, 32)))
    goto fail;

  if (hd_key->origin && !map_set_tagged(map, 6, CRYPTO_KEYPATH_TAG,
                                        keypath_to_data_item(hd_key->origin)))
    goto fail;

  if (hd_key->children &&
      !map_set_tagged(map, 7, CRYPTO_KEYPATH_TAG,
                      keypath_to_data_item(hd_key->children)))
    goto fail;

  if (hd_key->parent_fingerprint) {
    uint32_t fp = ((uint32_t)hd_key->parent_fingerprint[0] << 24) |
                  ((uint32_t)hd_key->parent_fingerprint[1] << 16) |
                  ((uint32_t)hd_key->parent_fingerprint[2] << 8) |
                  ((uint32_t)hd_key->parent_fingerprint[3]);
    if (!map_set_owned(map, 8, cbor_value_new_unsigned_int(fp)))
      goto fail;
  }

  return map;

fail:
  cbor_value_free(map);
  return NULL;
}

// Registry item interface
registry_item_t *hd_key_to_registry_item(hd_key_data_t *hd_key) {
  return registry_item_new(&HDKEY_TYPE, hd_key, NULL, hd_key_from_data_item);
}

hd_key_data_t *hd_key_from_registry_item(registry_item_t *item) {
  if (!item || item->type != &HDKEY_TYPE)
    return NULL;
  return (hd_key_data_t *)item->data;
}

// Generate BIP32 extended key (xpub/xprv format)
char *hd_key_bip32_key(hd_key_data_t *hd_key, bool include_derivation_path) {
  // An xpub needs a compressed public key and a chain code. Private key-data
  // (0x00 prefix, which includes every BCR-2020-007 master key) cannot be one.
  if (!hd_key || !hd_key->key || hd_key->key_len != 33 ||
      (hd_key->key[0] != 0x02 && hd_key->key[0] != 0x03) || !hd_key->chain_code)
    return NULL;

  // Build the 78-byte BIP32 key data
  uint8_t key_data[78];
  memset(key_data, 0, 78);

  // Detect network from BIP44 coin type in origin path
  // BIP44 path: m/purpose'/coin_type'/account'
  // coin_type: 0' = mainnet, 1' = testnet
  bool is_testnet = false;
  if (hd_key->origin && hd_key->origin->component_count >= 2) {
    // Check the second component (coin_type)
    path_component_t *coin_type = &hd_key->origin->components[1];
    if (coin_type->hardened && coin_type->index == 1) {
      is_testnet = true;
    }
  }

  // Version bytes (4 bytes)
  uint8_t version[4];
  if (is_testnet) {
    // tpub (testnet)
    version[0] = 0x04;
    version[1] = 0x35;
    version[2] = 0x87;
    version[3] = 0xCF;
  } else {
    // xpub (mainnet) - default
    version[0] = 0x04;
    version[1] = 0x88;
    version[2] = 0xB2;
    version[3] = 0x1E;
  }
  memcpy(key_data, version, 4);

  // Depth (1 byte)
  uint8_t depth = 0;
  if (!hd_key->master && hd_key->origin) {
    depth = hd_key->origin->depth >= 0
                ? (uint8_t)hd_key->origin->depth
                : (uint8_t)hd_key->origin->component_count;
  }
  key_data[4] = depth;

  // Parent fingerprint (4 bytes)
  uint8_t parent_fp[4] = {0, 0, 0, 0};
  bool source_is_parent = false;

  if (!hd_key->master) {
    if (hd_key->parent_fingerprint) {
      memcpy(parent_fp, hd_key->parent_fingerprint, 4);
    } else if (hd_key->origin && hd_key->origin->source_fingerprint &&
               hd_key->origin->component_count == 1) {
      memcpy(parent_fp, hd_key->origin->source_fingerprint, 4);
      source_is_parent = true;
    }
  }
  memcpy(key_data + 5, parent_fp, 4);

  // Child index (4 bytes, big-endian)
  uint32_t index = 0;
  if (!hd_key->master && hd_key->origin &&
      hd_key->origin->component_count > 0) {
    path_component_t *last =
        &hd_key->origin->components[hd_key->origin->component_count - 1];
    index = last->index;
    if (last->hardened) {
      index |= 0x80000000;
    }
  }
  key_data[9] = (index >> 24) & 0xFF;
  key_data[10] = (index >> 16) & 0xFF;
  key_data[11] = (index >> 8) & 0xFF;
  key_data[12] = index & 0xFF;

  memcpy(key_data + 13, hd_key->chain_code, 32);
  memcpy(key_data + 45, hd_key->key, 33);

  // Encode with base58check
  char *xpub = base58check_encode(key_data, 78);
  if (!xpub)
    return NULL;

  if (!include_derivation_path) {
    return xpub;
  }

  // Build full descriptor string with derivation paths
  char *derivation = NULL;
  if (hd_key->origin && hd_key->origin->source_fingerprint &&
      hd_key->origin->component_count > 0 && !source_is_parent) {
    // Format: [fingerprint/path]
    char fp_hex[9];
    sprintf(fp_hex, "%02x%02x%02x%02x", hd_key->origin->source_fingerprint[0],
            hd_key->origin->source_fingerprint[1],
            hd_key->origin->source_fingerprint[2],
            hd_key->origin->source_fingerprint[3]);

    // Failing to render the origin path must be fatal, not skipped:
    // keypath_to_string() returns NULL on allocation failure, which used to
    // reach strlen(NULL), and dropping the derivation silently would emit a
    // descriptor that means something different from the key it describes.
    char *path = keypath_to_string(hd_key->origin);
    if (!path) {
      free(xpub);
      return NULL;
    }
    derivation = safe_malloc(strlen(fp_hex) + strlen(path) + 4); // [fp/path]
    if (derivation) {
      sprintf(derivation, "[%s/%s]", fp_hex, path);
    }
    free(path);
    if (!derivation) {
      free(xpub);
      return NULL;
    }
  }

  char *child_derivation = NULL;
  if (hd_key->children && hd_key->children->component_count > 0) {
    // Format: /path  (fatal on failure, as above)
    char *child_path = keypath_to_string(hd_key->children);
    if (!child_path) {
      free(xpub);
      free(derivation);
      return NULL;
    }
    child_derivation = safe_malloc(strlen(child_path) + 2); // /path
    if (child_derivation) {
      sprintf(child_derivation, "/%s", child_path);
    }
    free(child_path);
    if (!child_derivation) {
      free(xpub);
      free(derivation);
      return NULL;
    }
  }

  // Concatenate: derivation + xpub + child_derivation
  size_t total_len = strlen(xpub) + 1;
  if (derivation)
    total_len += strlen(derivation);
  if (child_derivation)
    total_len += strlen(child_derivation);

  char *result = safe_malloc(total_len);
  if (!result) {
    free(xpub);
    free(derivation);
    free(child_derivation);
    return NULL;
  }

  result[0] = '\0';
  if (derivation)
    strcat(result, derivation);
  strcat(result, xpub);
  if (child_derivation)
    strcat(result, child_derivation);

  free(xpub);
  free(derivation);
  free(child_derivation);

  return result;
}

char *hd_key_descriptor_key(hd_key_data_t *hd_key) {
  return hd_key_bip32_key(hd_key, true);
}
