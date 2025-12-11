#ifndef URTYPES_REGISTRY_H
#define URTYPES_REGISTRY_H

#include "cbor_data.h"
#include <stdbool.h>
#include <stdint.h>

// Registry type definition
typedef struct {
  const char *name;
  uint64_t tag; // 0 means no tag
} registry_type_t;

// Registry item interface - all UR types implement these functions
typedef struct registry_item registry_item_t;

// Function pointers for registry item operations
typedef cbor_value_t *(*to_data_item_fn)(registry_item_t *item);
typedef registry_item_t *(*from_data_item_fn)(cbor_value_t *data_item);
typedef void (*free_fn)(registry_item_t *item);

// Registry item base structure
struct registry_item {
  registry_type_t *type;
  to_data_item_fn to_data_item;
  from_data_item_fn from_data_item;
  free_fn free_item;
  void *data; // Pointer to actual type-specific data
};

// Registry item helper functions
uint8_t *registry_item_to_cbor(registry_item_t *item, size_t *out_len);
registry_item_t *registry_item_from_cbor(const uint8_t *cbor_data, size_t len,
                                         from_data_item_fn from_data_item_func);

// Helper to get map value as specific type
cbor_value_t *get_map_value(cbor_value_t *map, int key);

#endif // URTYPES_REGISTRY_H
