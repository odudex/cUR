# Build only the UR transport layer (bytewords, fountain codes, multi-part
# assembly) and skip the src/types payload codecs. Integrators that do their
# own payload CBOR (e.g. Blockstream Jade) enable this to cut build time.
option(UR_ENVELOPE_ONLY "Build only the UR transport (exclude src/types)" OFF)

set(UR_ENVELOPE_SRCS
    "src/ur.c"
    "src/ur_encoder.c"
    "src/ur_decoder.c"
    "src/fountain_encoder.c"
    "src/fountain_decoder.c"
    "src/fountain_utils.c"
    "src/bytewords.c"
    "src/crc32.c"
    "src/utils.c"
)

set(UR_TYPES_SRCS
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
    "src/types/bip39.c"
)

set(UR_SRCS ${UR_ENVELOPE_SRCS})
if(NOT UR_ENVELOPE_ONLY)
    list(APPEND UR_SRCS ${UR_TYPES_SRCS})
endif()

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${UR_SRCS}
        INCLUDE_DIRS "src"
        REQUIRES mbedtls
    )

    # Route sha256_compat.h to the mbedTLS PSA API (hardware-accelerated on
    # ESP32P4). PC builds omit this flag and use the bundled src/sha256/sha256.c.
    target_compile_definitions(${COMPONENT_LIB} PUBLIC UR_USE_MBEDTLS_SHA256)
else()
    # Plain host build (e.g. added via add_subdirectory from a simulator or
    # test harness). Uses the bundled SHA-256 so it has no dependencies.
    add_library(ur STATIC ${UR_SRCS} "src/sha256/sha256.c")
    set_target_properties(ur PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(ur PUBLIC "src")
endif()
