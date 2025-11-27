#ifndef URTYPES_MULTI_KEY_H
#define URTYPES_MULTI_KEY_H

#include "ec_key.h"
#include "hd_key.h"
#include "registry.h"
#include <stddef.h>
#include <stdint.h>

// MultiKey type structure (no tag, embedded in other types)
typedef struct {
  uint32_t threshold;      // M in M-of-N multisig
  ec_key_data_t **ec_keys; // Array of EC keys
  size_t ec_key_count;
  hd_key_data_t **hd_keys; // Array of HD keys
  size_t hd_key_count;
} multi_key_data_t;

// Create and destroy MultiKey
multi_key_data_t *multi_key_new(uint32_t threshold);
void multi_key_free(multi_key_data_t *multi_key);

// Parse MultiKey from CBOR data item (map with keys)
multi_key_data_t *multi_key_from_data_item(cbor_value_t *data_item);

// Add keys to MultiKey
bool multi_key_add_ec_key(multi_key_data_t *multi_key, ec_key_data_t *ec_key);
bool multi_key_add_hd_key(multi_key_data_t *multi_key, hd_key_data_t *hd_key);

#endif // URTYPES_MULTI_KEY_H
