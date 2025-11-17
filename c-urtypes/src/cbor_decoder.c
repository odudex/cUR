#include "cbor_decoder.h"
#include "utils.h"
#include <string.h>
#include <math.h>

// CBOR major types
#define CBOR_MAJOR_UNSIGNED_INT  0
#define CBOR_MAJOR_NEGATIVE_INT  1
#define CBOR_MAJOR_BYTES         2
#define CBOR_MAJOR_STRING        3
#define CBOR_MAJOR_ARRAY         4
#define CBOR_MAJOR_MAP           5
#define CBOR_MAJOR_TAG           6
#define CBOR_MAJOR_SIMPLE        7

static void set_error(urtypes_cbor_decoder_t *decoder, const char *error) {
    if (decoder->error) {
        safe_free(decoder->error);
    }
    decoder->error = safe_strdup(error);
}

static bool read_byte(urtypes_cbor_decoder_t *decoder, uint8_t *out) {
    if (decoder->offset >= decoder->len) {
        set_error(decoder, "Unexpected end of data");
        return false;
    }
    *out = decoder->data[decoder->offset++];
    return true;
}

static bool read_bytes(urtypes_cbor_decoder_t *decoder, uint8_t *out, size_t count) {
    if (decoder->offset + count > decoder->len) {
        set_error(decoder, "Unexpected end of data");
        return false;
    }
    memcpy(out, decoder->data + decoder->offset, count);
    decoder->offset += count;
    return true;
}

static bool read_argument(urtypes_cbor_decoder_t *decoder, uint8_t additional, uint64_t *out) {
    if (additional < 24) {
        *out = additional;
        return true;
    } else if (additional == 24) {
        uint8_t byte;
        if (!read_byte(decoder, &byte)) return false;
        *out = byte;
        return true;
    } else if (additional == 25) {
        uint8_t bytes[2];
        if (!read_bytes(decoder, bytes, 2)) return false;
        *out = ((uint64_t)bytes[0] << 8) | bytes[1];
        return true;
    } else if (additional == 26) {
        uint8_t bytes[4];
        if (!read_bytes(decoder, bytes, 4)) return false;
        *out = ((uint64_t)bytes[0] << 24) | ((uint64_t)bytes[1] << 16) |
               ((uint64_t)bytes[2] << 8) | bytes[3];
        return true;
    } else if (additional == 27) {
        uint8_t bytes[8];
        if (!read_bytes(decoder, bytes, 8)) return false;
        *out = ((uint64_t)bytes[0] << 56) | ((uint64_t)bytes[1] << 48) |
               ((uint64_t)bytes[2] << 40) | ((uint64_t)bytes[3] << 32) |
               ((uint64_t)bytes[4] << 24) | ((uint64_t)bytes[5] << 16) |
               ((uint64_t)bytes[6] << 8) | bytes[7];
        return true;
    } else {
        set_error(decoder, "Invalid additional information");
        return false;
    }
}

static cbor_value_t *decode_value(urtypes_cbor_decoder_t *decoder);

static cbor_value_t *decode_unsigned_int(urtypes_cbor_decoder_t *decoder, uint8_t additional) {
    uint64_t value;
    if (!read_argument(decoder, additional, &value)) return NULL;
    return cbor_value_new_unsigned_int(value);
}

static cbor_value_t *decode_negative_int(urtypes_cbor_decoder_t *decoder, uint8_t additional) {
    uint64_t value;
    if (!read_argument(decoder, additional, &value)) return NULL;
    // CBOR negative integers are encoded as -1 - value
    int64_t result = -1 - (int64_t)value;
    return cbor_value_new_negative_int(result);
}

static cbor_value_t *decode_bytes(urtypes_cbor_decoder_t *decoder, uint8_t additional) {
    uint64_t len;
    if (!read_argument(decoder, additional, &len)) return NULL;

    if (len > SIZE_MAX) {
        set_error(decoder, "Byte string too large");
        return NULL;
    }

    uint8_t *data = safe_malloc((size_t)len);
    if (!data) {
        set_error(decoder, "Memory allocation failed");
        return NULL;
    }

    if (!read_bytes(decoder, data, (size_t)len)) {
        safe_free(data);
        return NULL;
    }

    cbor_value_t *value = cbor_value_new_bytes(data, (size_t)len);
    safe_free(data);
    return value;
}

static cbor_value_t *decode_string(urtypes_cbor_decoder_t *decoder, uint8_t additional) {
    uint64_t len;
    if (!read_argument(decoder, additional, &len)) return NULL;

    if (len > SIZE_MAX) {
        set_error(decoder, "String too large");
        return NULL;
    }

    char *str = safe_malloc((size_t)len + 1);
    if (!str) {
        set_error(decoder, "Memory allocation failed");
        return NULL;
    }

    if (!read_bytes(decoder, (uint8_t *)str, (size_t)len)) {
        safe_free(str);
        return NULL;
    }

    str[len] = '\0';
    cbor_value_t *value = cbor_value_new_string(str);
    safe_free(str);
    return value;
}

static cbor_value_t *decode_array(urtypes_cbor_decoder_t *decoder, uint8_t additional) {
    uint64_t count;
    if (!read_argument(decoder, additional, &count)) return NULL;

    if (count > SIZE_MAX) {
        set_error(decoder, "Array too large");
        return NULL;
    }

    cbor_value_t *array = cbor_value_new_array();
    if (!array) {
        set_error(decoder, "Memory allocation failed");
        return NULL;
    }

    for (size_t i = 0; i < (size_t)count; i++) {
        cbor_value_t *item = decode_value(decoder);
        if (!item) {
            cbor_value_free(array);
            return NULL;
        }

        if (!cbor_array_append(array, item)) {
            cbor_value_free(item);
            cbor_value_free(array);
            set_error(decoder, "Failed to append to array");
            return NULL;
        }
    }

    return array;
}

static cbor_value_t *decode_map(urtypes_cbor_decoder_t *decoder, uint8_t additional) {
    uint64_t count;
    if (!read_argument(decoder, additional, &count)) return NULL;

    if (count > SIZE_MAX) {
        set_error(decoder, "Map too large");
        return NULL;
    }

    cbor_value_t *map = cbor_value_new_map();
    if (!map) {
        set_error(decoder, "Memory allocation failed");
        return NULL;
    }

    for (size_t i = 0; i < (size_t)count; i++) {
        cbor_value_t *key = decode_value(decoder);
        if (!key) {
            cbor_value_free(map);
            return NULL;
        }

        cbor_value_t *value = decode_value(decoder);
        if (!value) {
            cbor_value_free(key);
            cbor_value_free(map);
            return NULL;
        }

        if (!cbor_map_set(map, key, value)) {
            cbor_value_free(value);
            cbor_value_free(key);
            cbor_value_free(map);
            set_error(decoder, "Failed to add to map");
            return NULL;
        }
    }

    return map;
}

static cbor_value_t *decode_tag(urtypes_cbor_decoder_t *decoder, uint8_t additional) {
    uint64_t tag;
    if (!read_argument(decoder, additional, &tag)) return NULL;

    cbor_value_t *content = decode_value(decoder);
    if (!content) return NULL;

    return cbor_value_new_tag(tag, content);
}

static cbor_value_t *decode_simple(urtypes_cbor_decoder_t *decoder, uint8_t additional) {
    if (additional < 20) {
        set_error(decoder, "Unsupported simple value");
        return NULL;
    } else if (additional == 20) {
        return cbor_value_new_bool(false);
    } else if (additional == 21) {
        return cbor_value_new_bool(true);
    } else if (additional == 22) {
        return cbor_value_new_null();
    } else if (additional == 23) {
        return cbor_value_new_undefined();
    } else if (additional == 25) {
        // Half precision float (16-bit)
        uint8_t bytes[2];
        if (!read_bytes(decoder, bytes, 2)) return NULL;
        uint16_t half = ((uint16_t)bytes[0] << 8) | bytes[1];

        // Convert half-precision to double
        int sign = (half >> 15) & 1;
        int exponent = (half >> 10) & 0x1F;
        int fraction = half & 0x3FF;

        double value;
        if (exponent == 0) {
            if (fraction == 0) {
                value = sign ? -0.0 : 0.0;
            } else {
                value = ldexp((double)fraction, -24);
                if (sign) value = -value;
            }
        } else if (exponent == 31) {
            if (fraction == 0) {
                value = sign ? -INFINITY : INFINITY;
            } else {
                value = NAN;
            }
        } else {
            value = ldexp(1.0 + (double)fraction / 1024.0, exponent - 15);
            if (sign) value = -value;
        }

        return cbor_value_new_float(value);
    } else if (additional == 26) {
        // Single precision float (32-bit)
        uint8_t bytes[4];
        if (!read_bytes(decoder, bytes, 4)) return NULL;

        uint32_t bits = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                       ((uint32_t)bytes[2] << 8) | bytes[3];
        float f;
        memcpy(&f, &bits, sizeof(float));

        return cbor_value_new_float((double)f);
    } else if (additional == 27) {
        // Double precision float (64-bit)
        uint8_t bytes[8];
        if (!read_bytes(decoder, bytes, 8)) return NULL;

        uint64_t bits = ((uint64_t)bytes[0] << 56) | ((uint64_t)bytes[1] << 48) |
                       ((uint64_t)bytes[2] << 40) | ((uint64_t)bytes[3] << 32) |
                       ((uint64_t)bytes[4] << 24) | ((uint64_t)bytes[5] << 16) |
                       ((uint64_t)bytes[6] << 8) | bytes[7];
        double d;
        memcpy(&d, &bits, sizeof(double));

        return cbor_value_new_float(d);
    } else {
        set_error(decoder, "Unsupported simple value");
        return NULL;
    }
}

static cbor_value_t *decode_value(urtypes_cbor_decoder_t *decoder) {
    uint8_t initial_byte;
    if (!read_byte(decoder, &initial_byte)) return NULL;

    uint8_t major_type = (initial_byte >> 5) & 0x07;
    uint8_t additional = initial_byte & 0x1F;

    switch (major_type) {
        case CBOR_MAJOR_UNSIGNED_INT:
            return decode_unsigned_int(decoder, additional);
        case CBOR_MAJOR_NEGATIVE_INT:
            return decode_negative_int(decoder, additional);
        case CBOR_MAJOR_BYTES:
            return decode_bytes(decoder, additional);
        case CBOR_MAJOR_STRING:
            return decode_string(decoder, additional);
        case CBOR_MAJOR_ARRAY:
            return decode_array(decoder, additional);
        case CBOR_MAJOR_MAP:
            return decode_map(decoder, additional);
        case CBOR_MAJOR_TAG:
            return decode_tag(decoder, additional);
        case CBOR_MAJOR_SIMPLE:
            return decode_simple(decoder, additional);
        default:
            set_error(decoder, "Invalid major type");
            return NULL;
    }
}

// Create and destroy decoder
urtypes_cbor_decoder_t *urtypes_cbor_decoder_new(const uint8_t *data, size_t len) {
    if (!data || len == 0) return NULL;

    urtypes_cbor_decoder_t *decoder = safe_malloc(sizeof(urtypes_cbor_decoder_t));
    if (!decoder) return NULL;

    decoder->data = data;
    decoder->len = len;
    decoder->offset = 0;
    decoder->error = NULL;

    return decoder;
}

void urtypes_cbor_decoder_free(urtypes_cbor_decoder_t *decoder) {
    if (!decoder) return;

    if (decoder->error) {
        safe_free(decoder->error);
    }
    safe_free(decoder);
}

// Decode CBOR value
cbor_value_t *urtypes_cbor_decoder_decode(urtypes_cbor_decoder_t *decoder) {
    if (!decoder) return NULL;

    return decode_value(decoder);
}

// Get error message
const char *urtypes_cbor_decoder_get_error(urtypes_cbor_decoder_t *decoder) {
    if (!decoder) return "Invalid decoder";
    return decoder->error;
}

// Convenience function to decode bytes to a value
cbor_value_t *cbor_decode(const uint8_t *data, size_t len) {
    if (!data || len == 0) return NULL;

    urtypes_cbor_decoder_t *decoder = urtypes_cbor_decoder_new(data, len);
    if (!decoder) return NULL;

    cbor_value_t *value = urtypes_cbor_decoder_decode(decoder);
    urtypes_cbor_decoder_free(decoder);

    return value;
}
