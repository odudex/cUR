// MicroPython bindings for bc-urtypes
// Implements Bytes, PSBT, and BIP39 types

#include "py/obj.h"
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objarray.h"

#include "src/bytes_type.h"
#include "src/psbt.h"
#include "src/bip39.h"
#include "src/output.h"

// Forward declarations of MicroPython types
static const mp_obj_type_t mp_type_urtypes_bytes;
static const mp_obj_type_t mp_type_urtypes_psbt;

// ============================================================================
// Bytes Type
// ============================================================================

typedef struct {
    mp_obj_base_t base;
    bytes_data_t *bytes;
} mp_obj_bytes_t;

// Bytes constructor
static mp_obj_t bytes_make_new(const mp_obj_type_t *type, size_t n_args,
                                size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    // Get bytes data from argument
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);

    mp_obj_bytes_t *self = m_new_obj(mp_obj_bytes_t);
    self->base.type = type;
    self->bytes = bytes_new((const uint8_t *)bufinfo.buf, bufinfo.len);

    if (!self->bytes) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to create Bytes");
    }

    return MP_OBJ_FROM_PTR(self);
}

// Bytes destructor
static mp_obj_t bytes_del(mp_obj_t self_in) {
    mp_obj_bytes_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->bytes) {
        bytes_free(self->bytes);
        self->bytes = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(bytes_del_obj, bytes_del);

// Bytes.to_cbor() - returns CBOR encoded bytes
static mp_obj_t bytes_to_cbor_py(mp_obj_t self_in) {
    mp_obj_bytes_t *self = MP_OBJ_TO_PTR(self_in);

    size_t cbor_len;
    uint8_t *cbor_data = bytes_to_cbor(self->bytes, &cbor_len);

    if (!cbor_data) {
        mp_raise_msg(&mp_type_RuntimeError, "Failed to encode Bytes to CBOR");
    }

    mp_obj_t result = mp_obj_new_bytes(cbor_data, cbor_len);
    free(cbor_data);

    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(bytes_to_cbor_obj, bytes_to_cbor_py);

// Bytes.from_cbor(cbor_data) - class method
static mp_obj_t bytes_from_cbor_py(mp_obj_t cbor_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(cbor_data_in, &bufinfo, MP_BUFFER_READ);

    bytes_data_t *bytes = bytes_from_cbor((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!bytes) {
        mp_raise_msg(&mp_type_ValueError, "Failed to decode Bytes from CBOR");
    }

    // Create Python object
    mp_obj_bytes_t *self = m_new_obj(mp_obj_bytes_t);
    self->base.type = &mp_type_urtypes_bytes;
    self->bytes = bytes;

    return MP_OBJ_FROM_PTR(self);
}
static MP_DEFINE_CONST_FUN_OBJ_1(bytes_from_cbor_obj, bytes_from_cbor_py);

// Bytes.data property - returns the raw bytes
static mp_obj_t bytes_data_py(mp_obj_t self_in) {
    mp_obj_bytes_t *self = MP_OBJ_TO_PTR(self_in);

    size_t len;
    const uint8_t *data = bytes_get_data(self->bytes, &len);

    return mp_obj_new_bytes(data, len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(bytes_data_obj, bytes_data_py);

// Bytes locals dict
static const mp_rom_map_elem_t bytes_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&bytes_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_cbor), MP_ROM_PTR(&bytes_to_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_from_cbor), MP_ROM_PTR(&bytes_from_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_data), MP_ROM_PTR(&bytes_data_obj) },
};
static MP_DEFINE_CONST_DICT(bytes_locals_dict, bytes_locals_dict_table);

// Bytes type definition
static const mp_obj_type_t mp_type_urtypes_bytes = {
    { &mp_type_type },
    .name = MP_QSTR_Bytes,
    .make_new = bytes_make_new,
    .locals_dict = (mp_obj_dict_t *)&bytes_locals_dict,
};

// ============================================================================
// PSBT Type
// ============================================================================

typedef struct {
    mp_obj_base_t base;
    psbt_data_t *psbt;
} mp_obj_psbt_t;

// PSBT constructor
static mp_obj_t psbt_make_new(const mp_obj_type_t *type, size_t n_args,
                               size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    // Get bytes data from argument
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);

    mp_obj_psbt_t *self = m_new_obj(mp_obj_psbt_t);
    self->base.type = type;
    self->psbt = psbt_new((const uint8_t *)bufinfo.buf, bufinfo.len);

    if (!self->psbt) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to create PSBT");
    }

    return MP_OBJ_FROM_PTR(self);
}

// PSBT destructor
static mp_obj_t psbt_del(mp_obj_t self_in) {
    mp_obj_psbt_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->psbt) {
        psbt_free(self->psbt);
        self->psbt = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(psbt_del_obj, psbt_del);

// PSBT.to_cbor() - returns CBOR encoded bytes
static mp_obj_t psbt_to_cbor_py(mp_obj_t self_in) {
    mp_obj_psbt_t *self = MP_OBJ_TO_PTR(self_in);

    size_t cbor_len;
    uint8_t *cbor_data = psbt_to_cbor(self->psbt, &cbor_len);

    if (!cbor_data) {
        mp_raise_msg(&mp_type_RuntimeError, "Failed to encode PSBT to CBOR");
    }

    mp_obj_t result = mp_obj_new_bytes(cbor_data, cbor_len);
    free(cbor_data);

    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(psbt_to_cbor_obj, psbt_to_cbor_py);

// PSBT.from_cbor(cbor_data) - class method
static mp_obj_t psbt_from_cbor_py(mp_obj_t cbor_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(cbor_data_in, &bufinfo, MP_BUFFER_READ);

    psbt_data_t *psbt = psbt_from_cbor((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!psbt) {
        mp_raise_msg(&mp_type_ValueError, "Failed to decode PSBT from CBOR");
    }

    // Create Python object
    mp_obj_psbt_t *self = m_new_obj(mp_obj_psbt_t);
    self->base.type = &mp_type_urtypes_psbt;
    self->psbt = psbt;

    return MP_OBJ_FROM_PTR(self);
}
static MP_DEFINE_CONST_FUN_OBJ_1(psbt_from_cbor_obj, psbt_from_cbor_py);

// PSBT.data property - returns the raw bytes
static mp_obj_t psbt_data_py(mp_obj_t self_in) {
    mp_obj_psbt_t *self = MP_OBJ_TO_PTR(self_in);

    size_t len;
    const uint8_t *data = psbt_get_data(self->psbt, &len);

    return mp_obj_new_bytes(data, len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(psbt_data_obj, psbt_data_py);

// PSBT locals dict
static const mp_rom_map_elem_t psbt_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&psbt_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_cbor), MP_ROM_PTR(&psbt_to_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_from_cbor), MP_ROM_PTR(&psbt_from_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_data), MP_ROM_PTR(&psbt_data_obj) },
};
static MP_DEFINE_CONST_DICT(psbt_locals_dict, psbt_locals_dict_table);

// PSBT type definition
static const mp_obj_type_t mp_type_urtypes_psbt = {
    { &mp_type_type },
    .name = MP_QSTR_PSBT,
    .make_new = psbt_make_new,
    .locals_dict = (mp_obj_dict_t *)&psbt_locals_dict,
};

// ============================================================================
// BIP39 Function
// ============================================================================

// BIP39.words_from_cbor(cbor_data) - static function
// Takes CBOR data and directly returns the list of words
static mp_obj_t bip39_words_from_cbor_py(mp_obj_t cbor_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(cbor_data_in, &bufinfo, MP_BUFFER_READ);

    // Decode CBOR to BIP39
    bip39_data_t *bip39 = bip39_from_cbor((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!bip39) {
        mp_raise_msg(&mp_type_ValueError, "Failed to decode BIP39 from CBOR");
    }

    // Get words
    size_t word_count;
    char **words = bip39_get_words(bip39, &word_count);

    // Create Python list
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < word_count; i++) {
        mp_obj_list_append(list, mp_obj_new_str(words[i], strlen(words[i])));
    }

    // Cleanup
    bip39_free(bip39);

    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(bip39_words_from_cbor_obj, bip39_words_from_cbor_py);

// BIP39 namespace object (not a type, just a namespace for the static method)
static const mp_rom_map_elem_t bip39_namespace_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_words_from_cbor), MP_ROM_PTR(&bip39_words_from_cbor_obj) },
};
static MP_DEFINE_CONST_DICT(bip39_namespace_locals_dict, bip39_namespace_locals_dict_table);

static const mp_obj_type_t mp_type_urtypes_bip39 = {
    { &mp_type_type },
    .name = MP_QSTR_BIP39,
    .locals_dict = (mp_obj_dict_t *)&bip39_namespace_locals_dict,
};

// ============================================================================
// Output Descriptor Function
// ============================================================================

// output_from_cbor(cbor_data) - module function
// Takes CBOR data and returns output descriptor string
static mp_obj_t output_from_cbor_py(mp_obj_t cbor_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(cbor_data_in, &bufinfo, MP_BUFFER_READ);

    // Decode CBOR to Output
    output_data_t *output = output_from_cbor((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!output) {
        mp_raise_msg(&mp_type_ValueError, "Failed to decode Output from CBOR");
    }

    // Generate descriptor string with checksum
    char *descriptor = output_descriptor(output, true);
    if (!descriptor) {
        output_free(output);
        mp_raise_msg(&mp_type_RuntimeError, "Failed to generate output descriptor");
    }

    // Create Python string from descriptor
    mp_obj_t result = mp_obj_new_str(descriptor, strlen(descriptor));

    // Cleanup
    free(descriptor);
    output_free(output);

    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(output_from_cbor_obj, output_from_cbor_py);

// Output descriptor from Account CBOR (extracts first output)
static mp_obj_t output_from_cbor_account_py(mp_obj_t cbor_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(cbor_data_in, &bufinfo, MP_BUFFER_READ);

    // Extract first output descriptor from Account CBOR
    char *descriptor = output_descriptor_from_cbor_account((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!descriptor) {
        mp_raise_msg(&mp_type_ValueError, "Failed to extract output descriptor from Account CBOR");
    }

    // Create Python string from descriptor
    mp_obj_t result = mp_obj_new_str(descriptor, strlen(descriptor));

    // Cleanup
    free(descriptor);

    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(output_from_cbor_account_obj, output_from_cbor_account_py);

// ============================================================================
// Module Definition
// ============================================================================

// Static string constants for UR type names (with hyphens)
static const mp_obj_str_t crypto_psbt_type_str = {{&mp_type_str}, 0, 11, (const byte*)"crypto-psbt"};
static const mp_obj_str_t crypto_bip39_type_str = {{&mp_type_str}, 0, 12, (const byte*)"crypto-bip39"};

// Module globals table
static const mp_rom_map_elem_t urtypes_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_uURTypes) },
    { MP_ROM_QSTR(MP_QSTR_Bytes), MP_ROM_PTR(&mp_type_urtypes_bytes) },
    { MP_ROM_QSTR(MP_QSTR_PSBT), MP_ROM_PTR(&mp_type_urtypes_psbt) },
    { MP_ROM_QSTR(MP_QSTR_BIP39), MP_ROM_PTR(&mp_type_urtypes_bip39) },
    // Module functions
    { MP_ROM_QSTR(MP_QSTR_output_from_cbor), MP_ROM_PTR(&output_from_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_output_from_cbor_account), MP_ROM_PTR(&output_from_cbor_account_obj) },
    // Tag constants (integers)
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_PSBT_TAG), MP_ROM_INT(CRYPTO_PSBT_TAG) },
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_BIP39_TAG), MP_ROM_INT(CRYPTO_BIP39_TAG) },
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_ACCOUNT_TAG), MP_ROM_INT(CRYPTO_ACCOUNT_TAG) },
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_OUTPUT_TAG), MP_ROM_INT(CRYPTO_OUTPUT_TAG) },
    // Type name constants (strings for UR type field - with proper hyphens)
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_PSBT_TYPE), MP_ROM_PTR(&crypto_psbt_type_str) },
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_BIP39_TYPE), MP_ROM_PTR(&crypto_bip39_type_str) },
};
static MP_DEFINE_CONST_DICT(urtypes_globals, urtypes_globals_table);

const mp_obj_module_t urtypes_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&urtypes_globals,
};

// Register the module
MP_REGISTER_MODULE(MP_QSTR_uURTypes, urtypes_module, MODULE_URTYPES_ENABLED);
