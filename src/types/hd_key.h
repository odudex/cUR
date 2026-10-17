#ifndef URTYPES_HD_KEY_H
#define URTYPES_HD_KEY_H

#include "keypath.h"
#include "registry.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// HDKey tag number
#define CRYPTO_HDKEY_TAG 303

// Coin-info tag number (the hd-key's use_info field)
#define CRYPTO_COININFO_TAG 305

// Network from use_info. Only BTC keys are accepted, so the coin type is not
// kept. UNSPECIFIED means the key has no use_info.
typedef enum {
  HD_KEY_NETWORK_UNSPECIFIED = 0,
  HD_KEY_NETWORK_MAINNET,
  HD_KEY_NETWORK_TESTNET
} hd_key_network_t;

// HDKey type structure
typedef struct {
  bool master;  // Is this a master key
  uint8_t *key; // Public key (33 bytes) or private key (32 bytes)
  size_t key_len;
  uint8_t *chain_code;  // Chain code (32 bytes, optional)
  uint8_t *private_key; // Private key data (optional)
  size_t private_key_len;
  keypath_data_t *origin;      // Origin path (optional)
  keypath_data_t *children;    // Children derivation path (optional)
  uint8_t *parent_fingerprint; // Parent fingerprint (4 bytes, optional)
  hd_key_network_t network;    // From use_info or the xpub/tpub version
  // Note: name and note omitted (not needed for descriptors)
} hd_key_data_t;

// HDKey registry type
extern registry_type_t HDKEY_TYPE;

// Create and destroy HDKey
UR_WARN_UNUSED_RESULT hd_key_data_t *hd_key_new(void);
void hd_key_free(hd_key_data_t *hd_key);

// Registry item interface for HDKey
UR_WARN_UNUSED_RESULT registry_item_t *
hd_key_to_registry_item(hd_key_data_t *hd_key);
hd_key_data_t *hd_key_from_registry_item(registry_item_t *item);

// CBOR conversion functions
UR_WARN_UNUSED_RESULT registry_item_t *
hd_key_from_data_item(cbor_value_t *data_item);
UR_WARN_UNUSED_RESULT cbor_value_t *hd_key_to_data_item(hd_key_data_t *hd_key);

// Generate BIP32 extended key with derivation paths (xpub format)
UR_WARN_UNUSED_RESULT char *hd_key_bip32_key(hd_key_data_t *hd_key,
                                             bool include_derivation_path);

// Generate descriptor key string (includes full derivation info)
UR_WARN_UNUSED_RESULT char *hd_key_descriptor_key(hd_key_data_t *hd_key);

#endif // URTYPES_HD_KEY_H
