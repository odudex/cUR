#include "output.h"
#include "cbor_decoder.h"
#include "utils.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// Output registry type (tag 308)
registry_type_t OUTPUT_TYPE = {.name = "crypto-output",
                               .tag = CRYPTO_OUTPUT_TAG};

// Script expression mapping (tag -> expression string)
static const script_expression_t SCRIPT_EXPRESSIONS[] = {
    {SCRIPT_EXPR_ADDR, "addr"},
    {SCRIPT_EXPR_SH, "sh"},
    {SCRIPT_EXPR_WSH, "wsh"},
    {SCRIPT_EXPR_PK, "pk"},
    {SCRIPT_EXPR_PKH, "pkh"},
    {SCRIPT_EXPR_WPKH, "wpkh"},
    {SCRIPT_EXPR_COMBO, "combo"},
    {SCRIPT_EXPR_MULTI, "multi"},
    {SCRIPT_EXPR_SORTEDMULTI, "sortedmulti"},
    {SCRIPT_EXPR_RAW, "raw"},
    {SCRIPT_EXPR_TR, "tr"},
    {SCRIPT_EXPR_COSIGNER, "cosigner"},
    {0, NULL} // Sentinel
};

const script_expression_t *get_script_expression_by_tag(uint64_t tag) {
  for (size_t i = 0; SCRIPT_EXPRESSIONS[i].expression != NULL; i++) {
    if (SCRIPT_EXPRESSIONS[i].tag == tag) {
      return &SCRIPT_EXPRESSIONS[i];
    }
  }
  return NULL;
}

// Create and destroy Output
output_data_t *output_new(void) {
  output_data_t *output = safe_malloc(sizeof(output_data_t));
  if (!output)
    return NULL;

  output->script_expressions = NULL;
  output->script_expression_count = 0;
  output->key_type = KEY_TYPE_HD;
  output->crypto_key.hd_key = NULL;

  return output;
}

void output_free(output_data_t *output) {
  if (!output)
    return;

  free(output->script_expressions);

  // Free the appropriate key type
  switch (output->key_type) {
  case KEY_TYPE_HD:
    hd_key_free(output->crypto_key.hd_key);
    break;
  case KEY_TYPE_MULTI:
    multi_key_free(output->crypto_key.multi_key);
    break;
  }

  free(output);
}

// Parse Output from CBOR data item
registry_item_t *output_from_data_item(cbor_value_t *data_item) {
  if (!data_item)
    return NULL;

  output_data_t *output = output_new();
  if (!output)
    return NULL;

  // Parse script expressions by unwrapping tags
  cbor_value_t *tmp_item = data_item;
  script_expression_t **expressions = NULL;
  size_t expr_count = 0;

  // Collect script expression tags by unwrapping nested tags
  while (tmp_item && cbor_value_get_type(tmp_item) == CBOR_TYPE_TAG) {
    uint64_t tag = cbor_value_get_tag(tmp_item);
    const script_expression_t *expr = get_script_expression_by_tag(tag);

    if (expr) {
      // Add this expression to the list
      expr_count++;
      script_expression_t **new_exprs =
          safe_realloc(expressions, expr_count * sizeof(script_expression_t *));
      if (!new_exprs) {
        free(expressions);
        output_free(output);
        return NULL;
      }
      expressions = new_exprs;
      expressions[expr_count - 1] = (script_expression_t *)expr;

      // Unwrap to the next level
      tmp_item = cbor_value_get_tag_content(tmp_item);
    } else {
      // This tag is not a script expression, must be a key type tag
      break;
    }
  }

  output->script_expressions = expressions;
  output->script_expression_count = expr_count;

  // Determine if this is a multisig output
  bool is_multi = false;
  if (expr_count > 0) {
    const script_expression_t *last_expr = expressions[expr_count - 1];
    if (strcmp(last_expr->expression, "multi") == 0 ||
        strcmp(last_expr->expression, "sortedmulti") == 0) {
      is_multi = true;
    }
  }

  // Get the innermost item (the actual key data)
  cbor_value_t *key_item = tmp_item;
  uint64_t key_tag = cbor_value_get_tag(key_item);

  // Parse the key based on type
  if (is_multi) {
    // Parse as MultiKey (map with threshold and keys)
    multi_key_data_t *multi_key = multi_key_from_data_item(key_item);
    if (!multi_key) {
      output_free(output);
      return NULL;
    }
    output->key_type = KEY_TYPE_MULTI;
    output->crypto_key.multi_key = multi_key;
  } else if (key_tag == CRYPTO_HDKEY_TAG) {
    // Parse as HDKey - unwrap the tag to get the map content
    cbor_value_t *key_content = cbor_value_get_tag_content(key_item);
    registry_item_t *item = hd_key_from_data_item(key_content);
    if (!item) {
      output_free(output);
      return NULL;
    }
    hd_key_data_t *hd_key = hd_key_from_registry_item(item);
    if (!hd_key) {
      free(item);
      output_free(output);
      return NULL;
    }
    item->data = NULL; // Transfer ownership
    free(item);
    output->key_type = KEY_TYPE_HD;
    output->crypto_key.hd_key = hd_key;
  } else {
    // Unknown key type
    output_free(output);
    return NULL;
  }

  return output_to_registry_item(output);
}

// Registry item interface
registry_item_t *output_to_registry_item(output_data_t *output) {
  if (!output)
    return NULL;

  registry_item_t *item = safe_malloc(sizeof(registry_item_t));
  if (!item)
    return NULL;

  item->type = &OUTPUT_TYPE;
  item->data = output;
  item->to_data_item = NULL; // Not needed for read-only
  item->from_data_item = output_from_data_item;
  item->free_item = NULL;

  return item;
}

output_data_t *output_from_registry_item(registry_item_t *item) {
  if (!item || item->type != &OUTPUT_TYPE)
    return NULL;
  return (output_data_t *)item->data;
}

output_data_t *output_from_cbor(const uint8_t *cbor_data, size_t len) {
  if (!cbor_data || len == 0)
    return NULL;

  registry_item_t *item =
      registry_item_from_cbor(cbor_data, len, output_from_data_item);
  if (!item)
    return NULL;

  output_data_t *output = output_from_registry_item(item);

  // Transfer ownership
  item->data = NULL;
  free(item);

  return output;
}

// Descriptor checksum algorithm (polymod)
static uint64_t polymod(uint64_t c, int val) {
  uint8_t c0 = (c >> 35) & 0xFF;
  c = ((c & 0x7FFFFFFFF) << 5) ^ val;
  if (c0 & 1)
    c ^= 0xF5DEE51989ULL;
  if (c0 & 2)
    c ^= 0xA9FDCA3312ULL;
  if (c0 & 4)
    c ^= 0x1BAB10E32DULL;
  if (c0 & 8)
    c ^= 0x3706B1677AULL;
  if (c0 & 16)
    c ^= 0x644D626FFDULL;
  return c;
}

static const char INPUT_CHARSET[] = "0123456789()[],'/"
                                    "*abcdefgh@:$%{}IJKLMNOPQRSTUVWXYZ&+-.;<=>?"
                                    "!^_|~ijklmnopqrstuvwxyzABCDEFGH`#\"\\ ";
static const char CHECKSUM_CHARSET[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

static char *descriptor_checksum(const char *descriptor) {
  if (!descriptor)
    return NULL;

  uint64_t c = 1;
  int cls = 0;
  int clscount = 0;

  for (const char *ptr = descriptor; *ptr; ptr++) {
    const char *pos_ptr = strchr(INPUT_CHARSET, *ptr);
    if (!pos_ptr)
      return NULL; // Invalid character

    int pos = pos_ptr - INPUT_CHARSET;
    c = polymod(c, pos & 31);
    cls = cls * 3 + (pos >> 5);
    clscount++;

    if (clscount == 3) {
      c = polymod(c, cls);
      cls = 0;
      clscount = 0;
    }
  }

  if (clscount > 0) {
    c = polymod(c, cls);
  }

  for (int i = 0; i < 8; i++) {
    c = polymod(c, 0);
  }
  c ^= 1;

  char *checksum = safe_malloc(9);
  if (!checksum)
    return NULL;

  for (int i = 0; i < 8; i++) {
    checksum[i] = CHECKSUM_CHARSET[(c >> (5 * (7 - i))) & 31];
  }
  checksum[8] = '\0';

  return checksum;
}

// Generate output descriptor string
char *output_descriptor(output_data_t *output, bool include_checksum) {
  if (!output)
    return NULL;

  byte_buffer_t *buf = byte_buffer_new();
  if (!buf)
    return NULL;

  // Write script expressions (opening)
  for (size_t i = 0; i < output->script_expression_count; i++) {
    const char *expr = output->script_expressions[i]->expression;
    byte_buffer_append(buf, (const uint8_t *)expr, strlen(expr));
    byte_buffer_append_byte(buf, '(');
  }

  // Write threshold for multisig
  if (output->key_type == KEY_TYPE_MULTI) {
    char threshold_str[16];
    sprintf(threshold_str, "%" PRIu32 ",", output->crypto_key.multi_key->threshold);
    byte_buffer_append(buf, (const uint8_t *)threshold_str,
                       strlen(threshold_str));
  }

  // Write keys
  if (output->key_type == KEY_TYPE_HD) {
    char *key_str = hd_key_descriptor_key(output->crypto_key.hd_key);
    if (key_str) {
      byte_buffer_append(buf, (const uint8_t *)key_str, strlen(key_str));
      free(key_str);
    }
  } else if (output->key_type == KEY_TYPE_MULTI) {
    multi_key_data_t *mk = output->crypto_key.multi_key;

    // Write HD keys
    for (size_t i = 0; i < mk->hd_key_count; i++) {
      char *key_str = hd_key_descriptor_key(mk->hd_keys[i]);
      if (key_str) {
        byte_buffer_append(buf, (const uint8_t *)key_str, strlen(key_str));
        free(key_str);
      }
      if (i < mk->hd_key_count - 1) {
        byte_buffer_append_byte(buf, ',');
      }
    }
  }

  // Write script expressions (closing)
  for (size_t i = 0; i < output->script_expression_count; i++) {
    byte_buffer_append_byte(buf, ')');
  }

  byte_buffer_append_byte(buf, '\0');

  char *descriptor = safe_strdup((char *)byte_buffer_get_data(buf));
  byte_buffer_free(buf);

  if (!descriptor)
    return NULL;

  if (include_checksum) {
    char *checksum = descriptor_checksum(descriptor);
    if (checksum) {
      size_t desc_len = strlen(descriptor);
      char *result =
          safe_malloc(desc_len + 1 + 8 + 1); // descriptor + # + checksum + null
      if (result) {
        sprintf(result, "%s#%s", descriptor, checksum);
        free(descriptor);
        free(checksum);
        return result;
      }
      free(checksum);
    }
  }

  return descriptor;
}

// Helper function to extract first output descriptor from Account CBOR
char *output_descriptor_from_cbor_account(const uint8_t *account_cbor,
                                          size_t len) {
  if (!account_cbor || len == 0)
    return NULL;

  // Decode CBOR
  cbor_value_t *cbor_val = cbor_decode(account_cbor, len);
  if (!cbor_val)
    return NULL;

  // Account CBOR is a plain map (not wrapped in a tag)
  // Map structure: { 1: master_fingerprint, 2: [outputs...] }
  if (cbor_value_get_type(cbor_val) != CBOR_TYPE_MAP) {
    cbor_value_free(cbor_val);
    return NULL;
  }

  cbor_value_t *map = cbor_val;

  // Get map key 2 (output_descriptors array)
  cbor_value_t *outputs_array = cbor_map_get_int(map, 2);
  if (!outputs_array || cbor_value_get_type(outputs_array) != CBOR_TYPE_ARRAY) {
    cbor_value_free(cbor_val);
    return NULL;
  }

  // Get array size and check it has at least one element
  size_t array_size = cbor_value_get_array_size(outputs_array);
  if (array_size == 0) {
    cbor_value_free(cbor_val);
    return NULL;
  }

  // Get first output from array
  cbor_value_t *first_output = cbor_value_get_array_item(outputs_array, 0);
  if (!first_output) {
    cbor_value_free(cbor_val);
    return NULL;
  }

  // Parse as Output
  registry_item_t *output_item = output_from_data_item(first_output);
  if (!output_item) {
    cbor_value_free(cbor_val);
    return NULL;
  }

  output_data_t *output = output_from_registry_item(output_item);
  if (!output) {
    free(output_item);
    cbor_value_free(cbor_val);
    return NULL;
  }

  // Generate descriptor string (with checksum)
  char *descriptor = output_descriptor(output, true);

  // Cleanup
  output_item->data = NULL; // Prevent double free
  free(output_item);
  cbor_value_free(cbor_val);
  output_free(output);

  return descriptor;
}
