#include "psbt.h"
#include "byte_buffer.h"
#include "cbor_decoder.h"
#include "cbor_encoder.h"
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
  return registry_item_new(&PSBT_TYPE, psbt, psbt_to_data_item,
                           psbt_from_data_item);
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
  registry_item_t item = {.type = &PSBT_TYPE,
                          .data = psbt,
                          .to_data_item = psbt_to_data_item,
                          .from_data_item = NULL,
                          .free_item = NULL};
  return registry_item_to_cbor(&item, out_len);
}

psbt_data_t *psbt_from_cbor(const uint8_t *cbor_data, size_t len) {
  return (psbt_data_t *)registry_item_unwrap_from_cbor(cbor_data, len,
                                                       psbt_from_data_item);
}

// Accessors
const uint8_t *psbt_get_data(psbt_data_t *psbt, size_t *out_len) {
  return bytes_get_data(psbt, out_len);
}
