//
// ur_decoder.c
//
// Copyright © 2025 Krux Contributors
// Licensed under the "BSD-2-Clause Plus Patent License"
//
// Implementation of Uniform Resources (UR) decoder following the specification:
// https://github.com/BlockchainCommons/Research/blob/master/papers/bcr-2020-005-ur.md
//
// This is an independent implementation written using
// foundation-ur-py as a reference for testing and validation.
//

#include "ur_decoder.h"
#include "bytewords.h"
#include "fountain_decoder.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

static fountain_encoder_part_t *
create_fountain_part_from_cbor(uint8_t *cbor_data, size_t cbor_len,
                               uint32_t seq_num, size_t seq_len,
                               bool take_ownership) {
  if (!cbor_data || cbor_len == 0)
    return NULL;

  fountain_encoder_part_t *part = safe_malloc(sizeof(fountain_encoder_part_t));
  if (!part)
    return NULL;

  part->seq_num = seq_num;
  part->seq_len = seq_len;
  part->message_len = 0;
  part->checksum = 0;
  part->data_len = cbor_len;

  if (take_ownership) {
    part->data = cbor_data;
  } else {
    part->data = safe_malloc(cbor_len);
    if (!part->data) {
      free(part);
      return NULL;
    }
    memcpy(part->data, cbor_data, cbor_len);
  }

  return part;
}

static void free_fountain_part(fountain_encoder_part_t *part) {
  if (part) {
    if (part->data) {
      free(part->data);
    }
    free(part);
  }
}

ur_decoder_t *ur_decoder_new(void) {
  ur_decoder_t *decoder = safe_malloc(sizeof(ur_decoder_t));
  if (!decoder)
    return NULL;

  decoder->fountain_decoder = fountain_decoder_new();
  if (!decoder->fountain_decoder) {
    free(decoder);
    return NULL;
  }

  decoder->expected_type = NULL;
  decoder->result = NULL;
  decoder->is_complete_flag = false;
  decoder->last_error = UR_DECODER_OK;

  return decoder;
}

void ur_decoder_free(ur_decoder_t *decoder) {
  if (!decoder)
    return;

  if (decoder->fountain_decoder) {
    fountain_decoder_free(decoder->fountain_decoder);
  }

  if (decoder->expected_type) {
    free(decoder->expected_type);
  }

  if (decoder->result) {
    ur_result_free(decoder->result);
  }

  free(decoder);
}

static bool validate_part_type(ur_decoder_t *decoder, const char *type) {
  if (!decoder || !type)
    return false;

  if (!decoder->expected_type) {
    if (!is_ur_type(type)) {
      decoder->last_error = UR_DECODER_ERROR_INVALID_TYPE;
      return false;
    }
    decoder->expected_type = safe_strdup(type);
    if (!decoder->expected_type) {
      decoder->last_error = UR_DECODER_ERROR_MEMORY;
      return false;
    }
    return true;
  }

  if (strcmp(decoder->expected_type, type) != 0) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_TYPE;
    return false;
  }

  return true;
}

static ur_result_t *decode_single_part(const char *type, const char *body) {
  if (!type || !body)
    return NULL;

  uint8_t *cbor_data;
  size_t cbor_len;
  if (!bytewords_decode_raw(body, &cbor_data, &cbor_len)) {
    return NULL;
  }

  ur_result_t *result = safe_malloc(sizeof(ur_result_t));
  if (!result) {
    free(cbor_data);
    return NULL;
  }

  result->type = safe_strdup(type);
  result->cbor_data = cbor_data;
  result->cbor_len = cbor_len;

  if (!result->type) {
    free(cbor_data);
    free(result);
    return NULL;
  }

  return result;
}

// CBOR unsigned integer parser (major type 0)
static bool cbor_read_uint32(const uint8_t **ptr, size_t *remaining,
                              uint32_t *value) {
  if (*remaining < 1)
    return false;

  uint8_t head = (*ptr)[0];
  if (head < 24) {
    *value = head;
    (*ptr)++;
    (*remaining)--;
  } else if (head == 24) {
    if (*remaining < 2)
      return false;
    *value = (*ptr)[1];
    *ptr += 2;
    *remaining -= 2;
  } else if (head == 25) {
    if (*remaining < 3)
      return false;
    *value = ((*ptr)[1] << 8) | (*ptr)[2];
    *ptr += 3;
    *remaining -= 3;
  } else if (head == 26) {
    if (*remaining < 5)
      return false;
    *value = ((uint32_t)(*ptr)[1] << 24) | ((uint32_t)(*ptr)[2] << 16) |
             ((uint32_t)(*ptr)[3] << 8) | (uint32_t)(*ptr)[4];
    *ptr += 5;
    *remaining -= 5;
  } else {
    return false;
  }
  return true;
}

// CBOR byte string parser (major type 2)
static bool cbor_read_bytes(const uint8_t **ptr, size_t *remaining,
                             const uint8_t **data, size_t *data_len) {
  if (*remaining < 1)
    return false;

  uint8_t head = (*ptr)[0];
  if (head >= 0x40 && head <= 0x57) {
    *data_len = head - 0x40;
    (*ptr)++;
    (*remaining)--;
  } else if (head == 0x58) {
    if (*remaining < 2)
      return false;
    *data_len = (*ptr)[1];
    *ptr += 2;
    *remaining -= 2;
  } else if (head == 0x59) {
    if (*remaining < 3)
      return false;
    *data_len = ((*ptr)[1] << 8) | (*ptr)[2];
    *ptr += 3;
    *remaining -= 3;
  } else if (head == 0x5a) {
    if (*remaining < 5)
      return false;
    *data_len = ((size_t)(*ptr)[1] << 24) | ((size_t)(*ptr)[2] << 16) |
                ((size_t)(*ptr)[3] << 8) | (size_t)(*ptr)[4];
    *ptr += 5;
    *remaining -= 5;
  } else {
    return false;
  }

  if (*remaining < *data_len)
    return false;

  *data = *ptr;
  return true;
}

bool ur_decoder_receive_part(ur_decoder_t *decoder, const char *part_str) {
  if (!decoder || !part_str) {
    if (decoder)
      decoder->last_error = UR_DECODER_ERROR_NULL_POINTER;
    return false;
  }

  if (decoder->is_complete_flag) {
    return false;
  }

  decoder->last_error = UR_DECODER_OK;

  char *type = NULL;
  char **components = NULL;
  size_t component_count = 0;
  uint8_t *cbor_data = NULL;
  bool result = false;

  if (!parse_ur_string(part_str, &type, &components, &component_count)) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_SCHEME;
    return false;
  }

  if (!validate_part_type(decoder, type))
    goto cleanup;

  if (component_count == 1) {
    decoder->result = decode_single_part(type, components[0]);
    if (decoder->result) {
      decoder->is_complete_flag = true;
      result = true;
    } else {
      decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    }
    goto cleanup;
  }

  if (component_count != 2) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_PATH_LENGTH;
    goto cleanup;
  }

  uint32_t seq_num;
  size_t seq_len;
  if (!parse_sequence_component(components[0], &seq_num, &seq_len)) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_SEQUENCE_COMPONENT;
    goto cleanup;
  }

  size_t cbor_len;
  if (!bytewords_decode_raw(components[1], &cbor_data, &cbor_len)) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    goto cleanup;
  }

  if (cbor_len < 5) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    goto cleanup;
  }

  const uint8_t *cbor_ptr = cbor_data;
  size_t remaining = cbor_len;

  // Expect CBOR array of 5 elements (0x85)
  if (remaining < 1 || cbor_ptr[0] != 0x85) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    goto cleanup;
  }
  cbor_ptr++;
  remaining--;

  // Parse 4 CBOR unsigned integers
  uint32_t cbor_seq_num, cbor_seq_len, cbor_message_len, cbor_checksum;
  uint32_t *values[] = {&cbor_seq_num, &cbor_seq_len, &cbor_message_len,
                        &cbor_checksum};
  for (int i = 0; i < 4; i++) {
    if (!cbor_read_uint32(&cbor_ptr, &remaining, values[i])) {
      decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
      goto cleanup;
    }
  }

  // Parse CBOR byte string (fragment data)
  const uint8_t *fragment_ptr;
  size_t fragment_len;
  if (!cbor_read_bytes(&cbor_ptr, &remaining, &fragment_ptr, &fragment_len)) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    goto cleanup;
  }

  // Allocate fragment data that the encoder part will take ownership of
  uint8_t *fragment_data = malloc(fragment_len);
  if (!fragment_data) {
    decoder->last_error = UR_DECODER_ERROR_MEMORY;
    goto cleanup;
  }
  memcpy(fragment_data, fragment_ptr, fragment_len);
  free(cbor_data);
  cbor_data = NULL;

  fountain_encoder_part_t *part = create_fountain_part_from_cbor(
      fragment_data, fragment_len, seq_num, seq_len, true);
  if (!part) {
    decoder->last_error = UR_DECODER_ERROR_MEMORY;
    goto cleanup;
  }
  part->message_len = cbor_message_len;
  part->checksum = cbor_checksum;

  bool success = fountain_decoder_receive_part(decoder->fountain_decoder, part);
  free_fountain_part(part);

  if (!success) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_PART;
    goto cleanup;
  }

  if (fountain_decoder_is_complete(decoder->fountain_decoder)) {
    if (fountain_decoder_is_success(decoder->fountain_decoder)) {
      decoder->result = safe_malloc(sizeof(ur_result_t));
      if (decoder->result) {
        decoder->result->type = safe_strdup(type);
        size_t result_len =
            fountain_decoder_result_message_len(decoder->fountain_decoder);
        uint8_t *result_data =
            fountain_decoder_result_message(decoder->fountain_decoder);

        if (result_data && result_len > 0) {
          decoder->result->cbor_data = safe_malloc(result_len);
          if (decoder->result->cbor_data) {
            memcpy(decoder->result->cbor_data, result_data, result_len);
            decoder->result->cbor_len = result_len;
            decoder->is_complete_flag = true;
          }
        }
      }
    } else {
      decoder->last_error = UR_DECODER_ERROR_INVALID_CHECKSUM;
      decoder->is_complete_flag = true;
    }
  }

  result = success;

cleanup:
  free(cbor_data);
  free(type);
  free_string_array(components, component_count);
  free(components);
  return result;
}

bool ur_decoder_is_complete(ur_decoder_t *decoder) {
  return decoder ? decoder->is_complete_flag : false;
}

bool ur_decoder_is_success(ur_decoder_t *decoder) {
  return decoder && decoder->is_complete_flag && decoder->result != NULL;
}

ur_result_t *ur_decoder_get_result(ur_decoder_t *decoder) {
  if (!decoder || !ur_decoder_is_success(decoder)) {
    return NULL;
  }
  return decoder->result;
}

size_t ur_decoder_expected_part_count(ur_decoder_t *decoder) {
  if (!decoder || !decoder->fountain_decoder)
    return 0;
  return fountain_decoder_expected_part_count(decoder->fountain_decoder);
}

size_t ur_decoder_processed_parts_count(ur_decoder_t *decoder) {
  if (!decoder || !decoder->fountain_decoder)
    return 0;
  return fountain_decoder_processed_parts_count(decoder->fountain_decoder);
}

double ur_decoder_estimated_percent_complete(ur_decoder_t *decoder) {
  if (!decoder || !decoder->fountain_decoder)
    return 0.0;
  return fountain_decoder_estimated_percent_complete(decoder->fountain_decoder);
}

ur_decoder_error_t ur_decoder_get_last_error(ur_decoder_t *decoder) {
  return decoder ? decoder->last_error : UR_DECODER_ERROR_NULL_POINTER;
}

void ur_result_free(ur_result_t *result) {
  if (!result)
    return;

  if (result->type) {
    free(result->type);
  }
  if (result->cbor_data) {
    free(result->cbor_data);
  }
  free(result);
}
