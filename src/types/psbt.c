#include "psbt.h"
#include "cbor_decoder.h"
#include "cbor_encoder.h"
#include "byte_buffer.h"
#include <string.h>

// PSBT registry type (tag 310)
registry_type_t PSBT_TYPE = {.name = "crypto-psbt", .tag = CRYPTO_PSBT_TAG};

// Create and destroy PSBT (just wrappers around bytes functions)
psbt_data_t *psbt_new(const uint8_t *data, size_t len) {
  return bytes_new(data, len);
}

void psbt_free(psbt_data_t *psbt) { bytes_free(psbt); }

// CBOR conversion functions
cbor_value_t *psbt_to_data_item(registry_item_t *item) {
  if (!item || !item->data)
    return NULL;

  psbt_data_t *psbt = (psbt_data_t *)item->data;

  // Return plain bytes, NO TAG (PSBT inherits from Bytes)
  return cbor_value_new_bytes(psbt->data, psbt->len);
}

registry_item_t *psbt_from_data_item(cbor_value_t *data_item) {
  if (!data_item)
    return NULL;

  // Expect plain bytes, NOT tagged (PSBT inherits from Bytes)
  if (cbor_value_get_type(data_item) != CBOR_TYPE_BYTES)
    return NULL;

  size_t len;
  const uint8_t *data = cbor_value_get_bytes(data_item, &len);
  // Allow NULL data if len is 0 (empty PSBT)
  if (!data && len > 0)
    return NULL;

  psbt_data_t *psbt = psbt_new(data, len);
  if (!psbt)
    return NULL;

  return psbt_to_registry_item(psbt);
}

// Registry item interface for PSBT
registry_item_t *psbt_to_registry_item(psbt_data_t *psbt) {
  if (!psbt)
    return NULL;

  registry_item_t *item = safe_malloc(sizeof(registry_item_t));
  if (!item)
    return NULL;

  item->type = &PSBT_TYPE;
  item->data = psbt;
  item->to_data_item = psbt_to_data_item;
  item->from_data_item = psbt_from_data_item;
  item->free_item = NULL;

  return item;
}

psbt_data_t *psbt_from_registry_item(registry_item_t *item) {
  if (!item || item->type != &PSBT_TYPE)
    return NULL;
  return (psbt_data_t *)item->data;
}

// Convenience functions
uint8_t *psbt_to_cbor(psbt_data_t *psbt, size_t *out_len) {
  if (!psbt || !out_len)
    return NULL;

  registry_item_t *item = psbt_to_registry_item(psbt);
  if (!item)
    return NULL;

  uint8_t *cbor_data = registry_item_to_cbor(item, out_len);

  // Free the registry item but not the psbt data (it's still owned by caller)
  free(item);

  return cbor_data;
}

psbt_data_t *psbt_from_cbor(const uint8_t *cbor_data, size_t len) {
  if (!cbor_data || len == 0)
    return NULL;

  registry_item_t *item =
      registry_item_from_cbor(cbor_data, len, psbt_from_data_item);
  if (!item)
    return NULL;

  psbt_data_t *psbt = psbt_from_registry_item(item);

  // Transfer ownership of psbt to caller and free the registry item wrapper
  item->data = NULL; // Don't free the psbt data
  free(item);

  return psbt;
}

// Accessors
const uint8_t *psbt_get_data(psbt_data_t *psbt, size_t *out_len) {
  return bytes_get_data(psbt, out_len);
}
