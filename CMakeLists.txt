idf_component_register(
    SRCS
        "src/ur.c"
        "src/ur_encoder.c"
        "src/ur_decoder.c"
        "src/fountain_encoder.c"
        "src/fountain_decoder.c"
        "src/fountain_utils.c"
        "src/bytewords.c"
        "src/crc32.c"
        "src/utils.c"
        "src/types/cbor_data.c"
        "src/types/bytes_type.c"
        "src/types/cbor_encoder.c"
        "src/types/cbor_decoder.c"
        "src/types/hd_key.c"
        "src/types/multi_key.c"
        "src/types/output.c"
        "src/types/psbt.c"
        "src/types/keypath.c"
        "src/types/registry.c"
        "src/types/byte_buffer.c"
    INCLUDE_DIRS
        "src"
    REQUIRES
        mbedtls mbedtls_compat
)

# Route sha256_compat.h to mbedtls (hardware-accelerated on ESP32P4).
# PC builds (Makefile) omit this flag and use the bundled src/sha256/sha256.c.
target_compile_definitions(${COMPONENT_LIB} PUBLIC UR_USE_MBEDTLS_SHA256)
