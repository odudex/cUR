// MicroPython bindings for bc-urtypes
// Implements Bytes, PSBT, and BIP39 types

#include "py/obj.h"
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objarray.h"

#include "src/bytes_type.h"
#include "src/psbt.h"
#include "src/bip39.h"

// Forward declarations of MicroPython types
static const mp_obj_type_t mp_type_urtypes_bytes;
static const mp_obj_type_t mp_type_urtypes_psbt;
static const mp_obj_type_t mp_type_urtypes_bip39;

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
// BIP39 Type
// ============================================================================

typedef struct {
    mp_obj_base_t base;
    bip39_data_t *bip39;
} mp_obj_bip39_t;

// BIP39 constructor
static mp_obj_t bip39_make_new(const mp_obj_type_t *type, size_t n_args,
                                size_t n_kw, const mp_obj_t *args) {
    enum { ARG_words, ARG_lang };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_words, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_lang, MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };
    mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args),
                               allowed_args, parsed_args);

    // Get words array
    mp_obj_t words_obj = parsed_args[ARG_words].u_obj;
    size_t word_count;
    mp_obj_t *word_items;
    mp_obj_get_array(words_obj, &word_count, &word_items);

    if (word_count == 0) {
        mp_raise_ValueError("Words array cannot be empty");
    }

    // Convert Python strings to C strings
    char **words = malloc(word_count * sizeof(char *));
    if (!words) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to allocate words array");
    }

    for (size_t i = 0; i < word_count; i++) {
        const char *word_str = mp_obj_str_get_str(word_items[i]);
        words[i] = (char *)word_str; // Temporary, will be copied by bip39_new
    }

    // Get lang if provided
    const char *lang = NULL;
    if (parsed_args[ARG_lang].u_obj != mp_const_none) {
        lang = mp_obj_str_get_str(parsed_args[ARG_lang].u_obj);
    }

    mp_obj_bip39_t *self = m_new_obj(mp_obj_bip39_t);
    self->base.type = type;
    self->bip39 = bip39_new(words, word_count, lang);

    free(words);

    if (!self->bip39) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to create BIP39");
    }

    return MP_OBJ_FROM_PTR(self);
}

// BIP39 destructor
static mp_obj_t bip39_del(mp_obj_t self_in) {
    mp_obj_bip39_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->bip39) {
        bip39_free(self->bip39);
        self->bip39 = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(bip39_del_obj, bip39_del);

// BIP39.to_cbor() - returns CBOR encoded bytes
static mp_obj_t bip39_to_cbor_py(mp_obj_t self_in) {
    mp_obj_bip39_t *self = MP_OBJ_TO_PTR(self_in);

    size_t cbor_len;
    uint8_t *cbor_data = bip39_to_cbor(self->bip39, &cbor_len);

    if (!cbor_data) {
        mp_raise_msg(&mp_type_RuntimeError, "Failed to encode BIP39 to CBOR");
    }

    mp_obj_t result = mp_obj_new_bytes(cbor_data, cbor_len);
    free(cbor_data);

    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(bip39_to_cbor_obj, bip39_to_cbor_py);

// BIP39.from_cbor(cbor_data) - class method
static mp_obj_t bip39_from_cbor_py(mp_obj_t cbor_data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(cbor_data_in, &bufinfo, MP_BUFFER_READ);

    bip39_data_t *bip39 = bip39_from_cbor((const uint8_t *)bufinfo.buf, bufinfo.len);
    if (!bip39) {
        mp_raise_msg(&mp_type_ValueError, "Failed to decode BIP39 from CBOR");
    }

    // Create Python object
    mp_obj_bip39_t *self = m_new_obj(mp_obj_bip39_t);
    self->base.type = &mp_type_urtypes_bip39;
    self->bip39 = bip39;

    return MP_OBJ_FROM_PTR(self);
}
static MP_DEFINE_CONST_FUN_OBJ_1(bip39_from_cbor_obj, bip39_from_cbor_py);

// BIP39.words property - returns list of words
static mp_obj_t bip39_words_py(mp_obj_t self_in) {
    mp_obj_bip39_t *self = MP_OBJ_TO_PTR(self_in);

    size_t word_count;
    char **words = bip39_get_words(self->bip39, &word_count);

    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < word_count; i++) {
        mp_obj_list_append(list, mp_obj_new_str(words[i], strlen(words[i])));
    }

    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(bip39_words_obj, bip39_words_py);

// BIP39.lang property - returns language code or None
static mp_obj_t bip39_lang_py(mp_obj_t self_in) {
    mp_obj_bip39_t *self = MP_OBJ_TO_PTR(self_in);

    const char *lang = bip39_get_lang(self->bip39);
    if (lang) {
        return mp_obj_new_str(lang, strlen(lang));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(bip39_lang_obj, bip39_lang_py);

// BIP39 locals dict
static const mp_rom_map_elem_t bip39_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&bip39_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_cbor), MP_ROM_PTR(&bip39_to_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_from_cbor), MP_ROM_PTR(&bip39_from_cbor_obj) },
    { MP_ROM_QSTR(MP_QSTR_words), MP_ROM_PTR(&bip39_words_obj) },
    { MP_ROM_QSTR(MP_QSTR_lang), MP_ROM_PTR(&bip39_lang_obj) },
};
static MP_DEFINE_CONST_DICT(bip39_locals_dict, bip39_locals_dict_table);

// BIP39 type definition
static const mp_obj_type_t mp_type_urtypes_bip39 = {
    { &mp_type_type },
    .name = MP_QSTR_BIP39,
    .make_new = bip39_make_new,
    .locals_dict = (mp_obj_dict_t *)&bip39_locals_dict,
};

// ============================================================================
// Module Definition
// ============================================================================

// Module globals table
static const mp_rom_map_elem_t urtypes_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_uURTypes) },
    { MP_ROM_QSTR(MP_QSTR_Bytes), MP_ROM_PTR(&mp_type_urtypes_bytes) },
    { MP_ROM_QSTR(MP_QSTR_PSBT), MP_ROM_PTR(&mp_type_urtypes_psbt) },
    { MP_ROM_QSTR(MP_QSTR_BIP39), MP_ROM_PTR(&mp_type_urtypes_bip39) },
    // Tag constants (integers)
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_PSBT_TAG), MP_ROM_INT(CRYPTO_PSBT_TAG) },
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_BIP39_TAG), MP_ROM_INT(CRYPTO_BIP39_TAG) },
    // Type name constants (strings for UR type field)
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_PSBT_TYPE), MP_ROM_QSTR(MP_QSTR_crypto_psbt) },
    { MP_ROM_QSTR(MP_QSTR_CRYPTO_BIP39_TYPE), MP_ROM_QSTR(MP_QSTR_crypto_bip39) },
};
static MP_DEFINE_CONST_DICT(urtypes_globals, urtypes_globals_table);

const mp_obj_module_t urtypes_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&urtypes_globals,
};

// Register the module
MP_REGISTER_MODULE(MP_QSTR_uURTypes, urtypes_module, MODULE_URTYPES_ENABLED);
