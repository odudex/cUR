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

// ============================================================================
// Bytes Functions
// ============================================================================

// bytes_from_cbor(cbor_data) - module function
// Takes CBOR data and directly returns the raw bytes
static mp_obj_t bytes_from_cbor_py(mp_obj_t cbor_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(cbor_data_in, &bufinfo, MP_BUFFER_READ);

    // Decode CBOR to Bytes
    bytes_data_t *bytes = bytes_from_cbor((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!bytes) {
        mp_raise_msg(&mp_type_ValueError, "Failed to decode Bytes from CBOR");
    }

    // Get raw bytes data
    size_t len;
    const uint8_t *data = bytes_get_data(bytes, &len);

    // Create Python bytes object
    mp_obj_t result = mp_obj_new_bytes(data, len);

    // Cleanup
    bytes_free(bytes);

    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(bytes_from_cbor_obj, bytes_from_cbor_py);

// bytes_to_cbor(bytes_data) - module function
// Takes raw bytes and returns CBOR encoded bytes
static mp_obj_t bytes_to_cbor_py(mp_obj_t bytes_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(bytes_data_in, &bufinfo, MP_BUFFER_READ);

    // Create Bytes from raw bytes
    bytes_data_t *bytes = bytes_new((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!bytes) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to create Bytes");
    }

    // Encode to CBOR
    size_t cbor_len;
    uint8_t *cbor_data = bytes_to_cbor(bytes, &cbor_len);
    if (!cbor_data) {
        bytes_free(bytes);
        mp_raise_msg(&mp_type_RuntimeError, "Failed to encode Bytes to CBOR");
    }

    // Create Python bytes object
    mp_obj_t result = mp_obj_new_bytes(cbor_data, cbor_len);

    // Cleanup
    free(cbor_data);
    bytes_free(bytes);

    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(bytes_to_cbor_obj, bytes_to_cbor_py);

// ============================================================================
// PSBT Functions
// ============================================================================

// psbt_from_cbor(cbor_data) - module function
// Takes CBOR data and directly returns the raw PSBT bytes
static mp_obj_t psbt_from_cbor_py(mp_obj_t cbor_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(cbor_data_in, &bufinfo, MP_BUFFER_READ);

    // Decode CBOR to PSBT
    psbt_data_t *psbt = psbt_from_cbor((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!psbt) {
        mp_raise_msg(&mp_type_ValueError, "Failed to decode PSBT from CBOR");
    }

    // Get raw PSBT data
    size_t len;
    const uint8_t *data = psbt_get_data(psbt, &len);

    // Create Python bytes object
    mp_obj_t result = mp_obj_new_bytes(data, len);

    // Cleanup
    psbt_free(psbt);

    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(psbt_from_cbor_obj, psbt_from_cbor_py);

// psbt_to_cbor(psbt_data) - module function
// Takes raw PSBT bytes and returns CBOR encoded bytes
static mp_obj_t psbt_to_cbor_py(mp_obj_t psbt_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(psbt_data_in, &bufinfo, MP_BUFFER_READ);

    // Create PSBT from raw bytes
    psbt_data_t *psbt = psbt_new((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!psbt) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to create PSBT");
    }

    // Encode to CBOR
    size_t cbor_len;
    uint8_t *cbor_data = psbt_to_cbor(psbt, &cbor_len);
    if (!cbor_data) {
        psbt_free(psbt);
        mp_raise_msg(&mp_type_RuntimeError, "Failed to encode PSBT to CBOR");
    }

    // Create Python bytes object
    mp_obj_t result = mp_obj_new_bytes(cbor_data, cbor_len);

    // Cleanup
    free(cbor_data);
    psbt_free(psbt);

    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(psbt_to_cbor_obj, psbt_to_cbor_py);

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
    { MP_ROM_QSTR(MP_QSTR_BIP39), MP_ROM_PTR(&mp_type_urtypes_bip39) },
    // Module functions
    { MP_ROM_QSTR(MP_QSTR_bytes_from_cbor), MP_ROM_PTR(&bytes_from_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_bytes_to_cbor), MP_ROM_PTR(&bytes_to_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_psbt_from_cbor), MP_ROM_PTR(&psbt_from_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_psbt_to_cbor), MP_ROM_PTR(&psbt_to_cbor_obj) },
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
