#include "registry.h"
#include "cbor_decoder.h"
#include "cbor_encoder.h"
#include "utils.h"

// Registry item helper functions
uint8_t *registry_item_to_cbor(registry_item_t *item, size_t *out_len) {
  if (!item || !out_len)
    return NULL;

  cbor_value_t *data_item = item->to_data_item(item);
  if (!data_item)
    return NULL;

  uint8_t *cbor_data = cbor_encode(data_item, out_len);
  cbor_value_free(data_item);

  return cbor_data;
}

registry_item_t *
registry_item_from_cbor(const uint8_t *cbor_data, size_t len,
                        from_data_item_fn from_data_item_func) {
  if (!cbor_data || len == 0 || !from_data_item_func)
    return NULL;

  cbor_value_t *data_item = cbor_decode(cbor_data, len);
  if (!data_item)
    return NULL;

  registry_item_t *item = from_data_item_func(data_item);
  cbor_value_free(data_item);

  return item;
}

// Helper to get map value as specific type
cbor_value_t *get_map_value(cbor_value_t *map, int key) {
  if (!map || cbor_value_get_type(map) != CBOR_TYPE_MAP)
    return NULL;
  return cbor_map_get_int(map, key);
}
