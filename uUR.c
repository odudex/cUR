#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/objexcept.h"
#include "src/ur_decoder.h"
#include "src/ur.h"
#include "src/ur_encoder.h"

// URDecoder class structure
typedef struct {
    mp_obj_base_t base;
    ur_decoder_t *decoder;
} mp_obj_ur_decoder_t;

// UREncoder class structure
typedef struct {
    mp_obj_base_t base;
    ur_encoder_t *encoder;
    mp_obj_t fountain_encoder_cached; // Cached fountain_encoder wrapper to prevent memory leaks
} mp_obj_ur_encoder_t;

// Forward declarations
static const mp_obj_type_t mp_type_ur_decoder;
static const mp_obj_type_t mp_type_ur_encoder;

// UR class structure for returning results (matches Python UR interface)
typedef struct {
    mp_obj_base_t base;
    ur_t *ur;
} mp_obj_ur_t;

static const mp_obj_type_t mp_type_ur;

// UR implementation (matches Python UR class)
static void ur_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_ur_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->ur) {
        mp_printf(print, "UR(type='%s')", ur_get_type(self->ur));
    } else {
        mp_printf(print, "UR(invalid)");
    }
}

static mp_obj_t ur_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 2, 2, false);

    // Extract type and CBOR data from args
    const char *ur_type = mp_obj_str_get_str(args[0]);

    mp_buffer_info_t cbor_buf;
    mp_get_buffer_raise(args[1], &cbor_buf, MP_BUFFER_READ);

    mp_obj_ur_t *self = m_new_obj(mp_obj_ur_t);
    self->base.type = type;

    // Create internal UR object
    self->ur = ur_new(ur_type, cbor_buf.buf, cbor_buf.len);
    if (!self->ur) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to create UR object");
    }

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t ur_del(mp_obj_t self_in) {
    mp_obj_ur_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->ur) {
        ur_free(self->ur);
        self->ur = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_del_obj, ur_del);

// UR attributes
static void ur_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_obj_ur_t *self = MP_OBJ_TO_PTR(self_in);

    if (dest[0] == MP_OBJ_NULL) {
        // Load attribute
        if (attr == MP_QSTR_type) {
            if (self->ur) {
                dest[0] = mp_obj_new_str(ur_get_type(self->ur), strlen(ur_get_type(self->ur)));
            } else {
                dest[0] = mp_const_none;
            }
        } else if (attr == MP_QSTR_cbor) {
            if (self->ur) {
                dest[0] = mp_obj_new_bytearray(ur_get_cbor_len(self->ur), ur_get_cbor(self->ur));
            } else {
                dest[0] = mp_const_none;
            }
        }
    }
}

// UR locals dict
static const mp_rom_map_elem_t ur_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&ur_del_obj) },
};
static MP_DEFINE_CONST_DICT(ur_locals_dict, ur_locals_dict_table);

static const mp_obj_type_t mp_type_ur = {
    { &mp_type_type },
    .name = MP_QSTR_UR,
    .print = ur_print,
    .make_new = ur_make_new,
    .attr = ur_attr,
    .locals_dict = (mp_obj_dict_t *)&ur_locals_dict,
};

// URDecoder implementation
static void ur_decoder_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);
    double progress = ur_decoder_estimated_percent_complete(self->decoder);
    mp_printf(print, "URDecoder(complete=%s, progress=%.1f%%)",
              ur_decoder_is_complete(self->decoder) ? "True" : "False",
              progress * 100.0);
}

static mp_obj_t ur_decoder_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    mp_obj_ur_decoder_t *self = m_new_obj(mp_obj_ur_decoder_t);
    self->base.type = type;
    self->decoder = ur_decoder_new();

    if (!self->decoder) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to create URDecoder");
    }

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t ur_decoder_del(mp_obj_t self_in) {
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->decoder) {
        ur_decoder_free(self->decoder);
        self->decoder = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_decoder_del_obj, ur_decoder_del);

// receive_part method
static mp_obj_t ur_decoder_receive_part_py(mp_obj_t self_in, mp_obj_t part_str) {
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (!self->decoder) {
        mp_raise_msg(&mp_type_RuntimeError, "URDecoder is closed");
    }

    const char *part_cstr = mp_obj_str_get_str(part_str);
    bool result = ur_decoder_receive_part(self->decoder, part_cstr);

    if (!result) {
        ur_decoder_error_t error = ur_decoder_get_last_error(self->decoder);
        switch (error) {
            case UR_DECODER_ERROR_INVALID_SCHEME:
                mp_raise_msg(&mp_type_ValueError, "Invalid UR scheme");
                break;
            case UR_DECODER_ERROR_INVALID_TYPE:
                mp_raise_msg(&mp_type_ValueError, "Invalid UR type");
                break;
            case UR_DECODER_ERROR_INVALID_PATH_LENGTH:
                mp_raise_msg(&mp_type_ValueError, "Invalid UR path length");
                break;
            case UR_DECODER_ERROR_INVALID_SEQUENCE_COMPONENT:
                mp_raise_msg(&mp_type_ValueError, "Invalid sequence component");
                break;
            case UR_DECODER_ERROR_INVALID_FRAGMENT:
                mp_raise_msg(&mp_type_ValueError, "Invalid fragment");
                break;
            default:
                mp_raise_msg(&mp_type_RuntimeError, "URDecoder error");
                break;
        }
    }

    return mp_obj_new_bool(result);
}
static MP_DEFINE_CONST_FUN_OBJ_2(ur_decoder_receive_part_obj, ur_decoder_receive_part_py);

// is_complete method
static mp_obj_t ur_decoder_is_complete_py(mp_obj_t self_in) {
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (!self->decoder) {
        return mp_const_false;
    }

    return mp_obj_new_bool(ur_decoder_is_complete(self->decoder));
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_decoder_is_complete_obj, ur_decoder_is_complete_py);

// is_success method
static mp_obj_t ur_decoder_is_success_py(mp_obj_t self_in) {
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (!self->decoder) {
        return mp_const_false;
    }

    return mp_obj_new_bool(ur_decoder_is_success(self->decoder));
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_decoder_is_success_obj, ur_decoder_is_success_py);

// result property
static mp_obj_t ur_decoder_result(mp_obj_t self_in) {
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (!self->decoder) {
        mp_raise_msg(&mp_type_RuntimeError, "URDecoder is closed");
    }

    // Check if decoding was successful before getting the result
    if (!ur_decoder_is_success(self->decoder)) {
        ur_decoder_error_t error = ur_decoder_get_last_error(self->decoder);
        switch (error) {
            case UR_DECODER_ERROR_INVALID_CHECKSUM:
                mp_raise_msg(&mp_type_ValueError, "Invalid checksum");
                break;
            case UR_DECODER_ERROR_INVALID_SCHEME:
                mp_raise_msg(&mp_type_ValueError, "Invalid UR scheme");
                break;
            case UR_DECODER_ERROR_INVALID_TYPE:
                mp_raise_msg(&mp_type_ValueError, "Invalid UR type");
                break;
            case UR_DECODER_ERROR_INVALID_PATH_LENGTH:
                mp_raise_msg(&mp_type_ValueError, "Invalid UR path length");
                break;
            case UR_DECODER_ERROR_INVALID_SEQUENCE_COMPONENT:
                mp_raise_msg(&mp_type_ValueError, "Invalid sequence component");
                break;
            case UR_DECODER_ERROR_INVALID_FRAGMENT:
                mp_raise_msg(&mp_type_ValueError, "Invalid fragment");
                break;
            case UR_DECODER_ERROR_INVALID_PART:
                mp_raise_msg(&mp_type_ValueError, "Invalid part");
                break;
            case UR_DECODER_ERROR_MEMORY:
                mp_raise_msg(&mp_type_RuntimeError, "Memory error");
                break;
            default:
                mp_raise_msg(&mp_type_RuntimeError, "URDecoder error");
                break;
        }
    }

    ur_result_t *result = ur_decoder_get_result(self->decoder);
    if (!result) {
        return mp_const_none;
    }

    // Create UR object (matches Python interface)
    mp_obj_t type_str = mp_obj_new_str(result->type, strlen(result->type));
    mp_obj_t cbor_bytes = mp_obj_new_bytearray(result->cbor_len, result->cbor_data);

    mp_obj_t args[2] = { type_str, cbor_bytes };
    return ur_make_new(&mp_type_ur, 2, 0, args);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_decoder_result_obj, ur_decoder_result);

// expected_part_count method
static mp_obj_t ur_decoder_expected_part_count_py(mp_obj_t self_in) {
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (!self->decoder) {
        return mp_obj_new_int(0);
    }

    return mp_obj_new_int(ur_decoder_expected_part_count(self->decoder));
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_decoder_expected_part_count_obj, ur_decoder_expected_part_count_py);

// processed_parts_count property
static mp_obj_t ur_decoder_processed_parts_count_py(mp_obj_t self_in) {
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (!self->decoder) {
        return mp_obj_new_int(0);
    }

    return mp_obj_new_int(ur_decoder_processed_parts_count(self->decoder));
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_decoder_processed_parts_count_obj, ur_decoder_processed_parts_count_py);

// estimated_percent_complete method
static mp_obj_t ur_decoder_estimated_percent_complete_py(mp_obj_t self_in) {
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (!self->decoder) {
        return mp_obj_new_float(0.0);
    }

    return mp_obj_new_float(ur_decoder_estimated_percent_complete(self->decoder));
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_decoder_estimated_percent_complete_obj, ur_decoder_estimated_percent_complete_py);

// URDecoder locals dict
static const mp_rom_map_elem_t ur_decoder_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&ur_decoder_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_receive_part), MP_ROM_PTR(&ur_decoder_receive_part_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_complete), MP_ROM_PTR(&ur_decoder_is_complete_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_success), MP_ROM_PTR(&ur_decoder_is_success_obj) },
    { MP_ROM_QSTR(MP_QSTR_estimated_percent_complete), MP_ROM_PTR(&ur_decoder_estimated_percent_complete_obj) },
};
static MP_DEFINE_CONST_DICT(ur_decoder_locals_dict, ur_decoder_locals_dict_table);

// URDecoder attributes
static void ur_decoder_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_obj_ur_decoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (dest[0] == MP_OBJ_NULL) {
        // Load attribute
        if (attr == MP_QSTR_result) {
            if (!self->decoder) {
                dest[0] = mp_const_none;
                return;
            }

            ur_result_t *result = ur_decoder_get_result(self->decoder);
            if (!result) {
                dest[0] = mp_const_none;
                return;
            }

            // Create UR object
            mp_obj_t type_str = mp_obj_new_str(result->type, strlen(result->type));
            mp_obj_t cbor_bytes = mp_obj_new_bytearray(result->cbor_len, result->cbor_data);

            mp_obj_t args[2] = { type_str, cbor_bytes };
            dest[0] = ur_make_new(&mp_type_ur, 2, 0, args);
        } else if (attr == MP_QSTR_expected_part_count) {
            if (!self->decoder) {
                dest[0] = mp_obj_new_int(0);
            } else {
                dest[0] = mp_obj_new_int(ur_decoder_expected_part_count(self->decoder));
            }
        } else if (attr == MP_QSTR_processed_parts_count) {
            if (!self->decoder) {
                dest[0] = mp_obj_new_int(0);
            } else {
                dest[0] = mp_obj_new_int(ur_decoder_processed_parts_count(self->decoder));
            }
        } else {
            // For other attributes (methods), check locals_dict explicitly
            mp_obj_dict_t *locals_dict = (mp_obj_dict_t *)&ur_decoder_locals_dict;
            mp_map_elem_t *elem = mp_map_lookup(&locals_dict->map, MP_OBJ_NEW_QSTR(attr), MP_MAP_LOOKUP);
            if (elem != NULL) {
                // Create a bound method
                dest[0] = mp_obj_new_bound_meth(elem->value, self_in);
            }
        }
    }
}

// URDecoder type definition
static const mp_obj_type_t mp_type_ur_decoder = {
    { &mp_type_type },
    .name = MP_QSTR_URDecoder,
    .print = ur_decoder_print,
    .make_new = ur_decoder_make_new,
    .attr = ur_decoder_attr,
    .locals_dict = (mp_obj_dict_t *)&ur_decoder_locals_dict,
};

// UREncoder implementation
static void ur_encoder_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_ur_encoder_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->encoder) {
        mp_printf(print, "UREncoder(seq_len=%u, complete=%s)",
                  ur_encoder_seq_len(self->encoder),
                  ur_encoder_is_complete(self->encoder) ? "True" : "False");
    } else {
        mp_printf(print, "UREncoder(invalid)");
    }
}

static mp_obj_t ur_encoder_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_ur, ARG_max_fragment_len, ARG_first_seq_num, ARG_min_fragment_len };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_ur, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_max_fragment_len, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_first_seq_num, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_min_fragment_len, MP_ARG_INT, {.u_int = 10} },
    };

    mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);

    // Extract UR object
    mp_obj_ur_t *ur_obj = MP_OBJ_TO_PTR(parsed_args[ARG_ur].u_obj);
    if (!mp_obj_is_type(parsed_args[ARG_ur].u_obj, &mp_type_ur)) {
        mp_raise_TypeError("First argument must be a UR object");
    }

    if (!ur_obj->ur) {
        mp_raise_msg(&mp_type_ValueError, "Invalid UR object");
    }

    // Extract parameters
    size_t max_fragment_len = parsed_args[ARG_max_fragment_len].u_int;
    uint32_t first_seq_num = parsed_args[ARG_first_seq_num].u_int;
    size_t min_fragment_len = parsed_args[ARG_min_fragment_len].u_int;

    // Create encoder
    mp_obj_ur_encoder_t *self = m_new_obj(mp_obj_ur_encoder_t);
    self->base.type = type;
    self->fountain_encoder_cached = MP_OBJ_NULL; // Initialize cache to NULL

    // Create internal encoder
    const char *ur_type = ur_get_type(ur_obj->ur);
    const uint8_t *cbor_data = ur_get_cbor(ur_obj->ur);
    size_t cbor_len = ur_get_cbor_len(ur_obj->ur);

    self->encoder = ur_encoder_new(ur_type, cbor_data, cbor_len,
                                    max_fragment_len, first_seq_num, min_fragment_len);
    if (!self->encoder) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to create UREncoder");
    }

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t ur_encoder_del(mp_obj_t self_in) {
    mp_obj_ur_encoder_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->encoder) {
        ur_encoder_free(self->encoder);
        self->encoder = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_encoder_del_obj, ur_encoder_del);

// next_part method
static mp_obj_t ur_encoder_next_part_py(mp_obj_t self_in) {
    mp_obj_ur_encoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (!self->encoder) {
        mp_raise_msg(&mp_type_RuntimeError, "UREncoder is closed");
    }

    char *ur_part = NULL;
    bool result = ur_encoder_next_part(self->encoder, &ur_part);

    if (!result) {
        if (ur_part) {
            free(ur_part);
        }
        mp_raise_msg(&mp_type_RuntimeError, "Failed to generate next part");
    }

    if (!ur_part) {
        mp_raise_msg(&mp_type_RuntimeError, "Generated NULL part");
    }

    // Copy the string into MicroPython's memory space
    size_t len = strlen(ur_part);
    mp_obj_t part_str = mp_obj_new_str_copy(&mp_type_str, (const byte*)ur_part, len);

    // Free the C-allocated string
    free(ur_part);

    return part_str;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_encoder_next_part_obj, ur_encoder_next_part_py);

// is_complete method
static mp_obj_t ur_encoder_is_complete_py(mp_obj_t self_in) {
    mp_obj_ur_encoder_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->encoder) {
        return mp_const_false;
    }
    return mp_obj_new_bool(ur_encoder_is_complete(self->encoder));
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_encoder_is_complete_obj, ur_encoder_is_complete_py);

// is_single_part method
static mp_obj_t ur_encoder_is_single_part_py(mp_obj_t self_in) {
    mp_obj_ur_encoder_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->encoder) {
        return mp_const_false;
    }
    return mp_obj_new_bool(ur_encoder_is_single_part(self->encoder));
}
static MP_DEFINE_CONST_FUN_OBJ_1(ur_encoder_is_single_part_obj, ur_encoder_is_single_part_py);

// UREncoder locals dict
static const mp_rom_map_elem_t ur_encoder_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&ur_encoder_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_next_part), MP_ROM_PTR(&ur_encoder_next_part_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_complete), MP_ROM_PTR(&ur_encoder_is_complete_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_single_part), MP_ROM_PTR(&ur_encoder_is_single_part_obj) },
};
static MP_DEFINE_CONST_DICT(ur_encoder_locals_dict, ur_encoder_locals_dict_table);

// FountainEncoder wrapper type for accessing seq_len()
typedef struct {
    mp_obj_base_t base;
    ur_encoder_t *encoder;
} mp_obj_fountain_encoder_wrapper_t;

static void fountain_encoder_wrapper_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_fountain_encoder_wrapper_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->encoder && self->encoder->fountain_encoder) {
        mp_printf(print, "FountainEncoder(seq_len=%u)", ur_encoder_seq_len(self->encoder));
    } else {
        mp_printf(print, "FountainEncoder(invalid)");
    }
}

static mp_obj_t fountain_encoder_seq_len_py(mp_obj_t self_in) {
    mp_obj_fountain_encoder_wrapper_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->encoder || !self->encoder->fountain_encoder) {
        return mp_obj_new_int(0);
    }
    return mp_obj_new_int(ur_encoder_seq_len(self->encoder));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fountain_encoder_seq_len_obj, fountain_encoder_seq_len_py);

static const mp_rom_map_elem_t fountain_encoder_wrapper_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_seq_len), MP_ROM_PTR(&fountain_encoder_seq_len_obj) },
};
static MP_DEFINE_CONST_DICT(fountain_encoder_wrapper_locals_dict, fountain_encoder_wrapper_locals_dict_table);

static const mp_obj_type_t mp_type_fountain_encoder_wrapper = {
    { &mp_type_type },
    .name = MP_QSTR_FountainEncoder,
    .print = fountain_encoder_wrapper_print,
    .locals_dict = (mp_obj_dict_t *)&fountain_encoder_wrapper_locals_dict,
};

// UREncoder attributes
static void ur_encoder_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_obj_ur_encoder_t *self = MP_OBJ_TO_PTR(self_in);

    if (dest[0] == MP_OBJ_NULL) {
        // Load attribute
        if (attr == MP_QSTR_fountain_encoder) {
            // Return cached fountain encoder wrapper to prevent memory leaks
            if (!self->encoder || !self->encoder->fountain_encoder) {
                dest[0] = mp_const_none;
                return;
            }

            // If not cached yet, create and cache the wrapper
            if (self->fountain_encoder_cached == MP_OBJ_NULL) {
                mp_obj_fountain_encoder_wrapper_t *fe_wrapper = m_new_obj(mp_obj_fountain_encoder_wrapper_t);
                fe_wrapper->base.type = &mp_type_fountain_encoder_wrapper;
                fe_wrapper->encoder = self->encoder;
                self->fountain_encoder_cached = MP_OBJ_FROM_PTR(fe_wrapper);
            }

            dest[0] = self->fountain_encoder_cached;
        } else {
            // For other attributes (methods), check locals_dict explicitly
            mp_obj_dict_t *locals_dict = (mp_obj_dict_t *)&ur_encoder_locals_dict;
            mp_map_elem_t *elem = mp_map_lookup(&locals_dict->map, MP_OBJ_NEW_QSTR(attr), MP_MAP_LOOKUP);
            if (elem != NULL) {
                // Create a bound method
                dest[0] = mp_obj_new_bound_meth(elem->value, self_in);
            }
        }
    }
}

// UREncoder type definition
static const mp_obj_type_t mp_type_ur_encoder = {
    { &mp_type_type },
    .name = MP_QSTR_UREncoder,
    .print = ur_encoder_print,
    .make_new = ur_encoder_make_new,
    .attr = ur_encoder_attr,
    .locals_dict = (mp_obj_dict_t *)&ur_encoder_locals_dict,
};

// Module globals table
static const mp_rom_map_elem_t bc_ur_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_bc_ur) },
    { MP_ROM_QSTR(MP_QSTR_URDecoder), MP_ROM_PTR(&mp_type_ur_decoder) },
    { MP_ROM_QSTR(MP_QSTR_UREncoder), MP_ROM_PTR(&mp_type_ur_encoder) },
    { MP_ROM_QSTR(MP_QSTR_UR), MP_ROM_PTR(&mp_type_ur) },
};
static MP_DEFINE_CONST_DICT(bc_ur_globals, bc_ur_globals_table);

// Module definition
const mp_obj_module_t bc_ur_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&bc_ur_globals,
};

MP_REGISTER_MODULE(MP_QSTR_uUR, bc_ur_module, MODULE_BC_UR_ENABLED);