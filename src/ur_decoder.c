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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fountain_encoder_part_t *
create_fountain_part_from_cbor(const uint8_t *cbor_data, size_t cbor_len,
                               uint32_t seq_num, size_t seq_len) {
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

  part->data = safe_malloc(cbor_len);
  if (!part->data) {
    free(part);
    return NULL;
  }

  memcpy(part->data, cbor_data, cbor_len);
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
  if (!bytewords_decode_raw(body, &cbor_data,
                            &cbor_len)) {
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

  char *type;
  char **components;
  size_t component_count;

  if (!parse_ur_string(part_str, &type, &components, &component_count)) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_SCHEME;
    return false;
  }

  if (!validate_part_type(decoder, type)) {
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }

  if (component_count == 1) {
    decoder->result = decode_single_part(type, components[0]);
    if (decoder->result) {
      decoder->is_complete_flag = true;
    } else {
      decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    }

    free(type);
    free_string_array(components, component_count);
    free(components);
    return decoder->result != NULL;
  }

  if (component_count != 2) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_PATH_LENGTH;
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }

  uint32_t seq_num;
  size_t seq_len;
  if (!parse_sequence_component(components[0], &seq_num, &seq_len)) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_SEQUENCE_COMPONENT;
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }

  uint8_t *cbor_data;
  size_t cbor_len;
  if (!bytewords_decode_raw(components[1], &cbor_data,
                            &cbor_len)) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }

  if (cbor_len < 5) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    free(cbor_data);
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }

  const uint8_t *cbor_ptr = cbor_data;
  size_t remaining = cbor_len;

  if (remaining < 1 || cbor_ptr[0] != 0x85) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    free(cbor_data);
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }
  cbor_ptr++;
  remaining--;

  uint32_t cbor_seq_num = 0, cbor_seq_len = 0, cbor_message_len = 0,
           cbor_checksum = 0;
  uint32_t *values[] = {&cbor_seq_num, &cbor_seq_len, &cbor_message_len,
                        &cbor_checksum};

  for (int i = 0; i < 4; i++) {
    if (remaining < 1) {
      decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
      free(cbor_data);
      free(type);
      free_string_array(components, component_count);
      free(components);
      return false;
    }

    if (cbor_ptr[0] < 24) {
      *values[i] = cbor_ptr[0];
      cbor_ptr++;
      remaining--;
    } else if (cbor_ptr[0] == 24) {
      if (remaining < 2) {
        decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
        free(cbor_data);
        free(type);
        free_string_array(components, component_count);
        free(components);
        return false;
      }
      *values[i] = cbor_ptr[1];
      cbor_ptr += 2;
      remaining -= 2;
    } else if (cbor_ptr[0] == 25) {
      if (remaining < 3) {
        decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
        free(cbor_data);
        free(type);
        free_string_array(components, component_count);
        free(components);
        return false;
      }
      *values[i] = (cbor_ptr[1] << 8) | cbor_ptr[2];
      cbor_ptr += 3;
      remaining -= 3;
    } else if (cbor_ptr[0] == 26) {
      if (remaining < 5) {
        decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
        free(cbor_data);
        free(type);
        free_string_array(components, component_count);
        free(components);
        return false;
      }
      *values[i] = (cbor_ptr[1] << 24) | (cbor_ptr[2] << 16) |
                   (cbor_ptr[3] << 8) | cbor_ptr[4];
      cbor_ptr += 5;
      remaining -= 5;
    } else {
      decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
      free(cbor_data);
      free(type);
      free_string_array(components, component_count);
      free(components);
      return false;
    }
  }

  if (remaining < 2) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    free(cbor_data);
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }

  size_t fragment_len = 0;
  if (cbor_ptr[0] >= 0x40 && cbor_ptr[0] <= 0x57) {
    fragment_len = cbor_ptr[0] - 0x40;
    cbor_ptr++;
    remaining--;
  } else if (cbor_ptr[0] == 0x58) {
    if (remaining < 2) {
      decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
      free(cbor_data);
      free(type);
      free_string_array(components, component_count);
      free(components);
      return false;
    }
    fragment_len = cbor_ptr[1];
    cbor_ptr += 2;
    remaining -= 2;
  } else if (cbor_ptr[0] == 0x59) {
    if (remaining < 3) {
      decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
      free(cbor_data);
      free(type);
      free_string_array(components, component_count);
      free(components);
      return false;
    }
    fragment_len = (cbor_ptr[1] << 8) | cbor_ptr[2];
    cbor_ptr += 3;
    remaining -= 3;
  } else if (cbor_ptr[0] == 0x5a) {
    if (remaining < 5) {
      decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
      free(cbor_data);
      free(type);
      free_string_array(components, component_count);
      free(components);
      return false;
    }
    fragment_len = (cbor_ptr[1] << 24) | (cbor_ptr[2] << 16) |
                   (cbor_ptr[3] << 8) | cbor_ptr[4];
    cbor_ptr += 5;
    remaining -= 5;
  } else {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    free(cbor_data);
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }

  if (remaining < fragment_len) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_FRAGMENT;
    free(cbor_data);
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }

  uint8_t *fragment_data = safe_malloc(fragment_len);
  if (!fragment_data) {
    decoder->last_error = UR_DECODER_ERROR_MEMORY;
    free(cbor_data);
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }
  memcpy(fragment_data, cbor_ptr, fragment_len);

  fountain_encoder_part_t *part = create_fountain_part_from_cbor(
      fragment_data, fragment_len, seq_num, seq_len);
  if (part) {
    part->message_len = cbor_message_len;
    part->checksum = cbor_checksum;
  }
  free(fragment_data);
  free(cbor_data);

  if (!part) {
    decoder->last_error = UR_DECODER_ERROR_MEMORY;
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
  }

  bool success = fountain_decoder_receive_part(decoder->fountain_decoder, part);
  free_fountain_part(part);

  if (!success) {
    decoder->last_error = UR_DECODER_ERROR_INVALID_PART;
    free(type);
    free_string_array(components, component_count);
    free(components);
    return false;
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

  free(type);
  free_string_array(components, component_count);
  free(components);
  return success;
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