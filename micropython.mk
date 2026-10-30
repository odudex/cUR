# MicroPython USERMOD wiring for the uUR module (bundled SHA-256).

UUR_MOD_DIR := $(USERMOD_DIR)

SRC_USERMOD += \
    $(UUR_MOD_DIR)/uUR.c \
    $(UUR_MOD_DIR)/src/ur.c \
    $(UUR_MOD_DIR)/src/ur_encoder.c \
    $(UUR_MOD_DIR)/src/ur_decoder.c \
    $(UUR_MOD_DIR)/src/fountain_encoder.c \
    $(UUR_MOD_DIR)/src/fountain_decoder.c \
    $(UUR_MOD_DIR)/src/fountain_utils.c \
    $(UUR_MOD_DIR)/src/bytewords.c \
    $(UUR_MOD_DIR)/src/crc32.c \
    $(UUR_MOD_DIR)/src/utils.c \
    $(UUR_MOD_DIR)/src/sha256/sha256.c \
    $(UUR_MOD_DIR)/src/types/cbor_data.c \
    $(UUR_MOD_DIR)/src/types/cbor_encoder.c \
    $(UUR_MOD_DIR)/src/types/cbor_decoder.c \
    $(UUR_MOD_DIR)/src/types/byte_buffer.c \
    $(UUR_MOD_DIR)/src/types/bytes_type.c \
    $(UUR_MOD_DIR)/src/types/bip39.c \
    $(UUR_MOD_DIR)/src/types/keypath.c \
    $(UUR_MOD_DIR)/src/types/hd_key.c \
    $(UUR_MOD_DIR)/src/types/multi_key.c \
    $(UUR_MOD_DIR)/src/types/output.c \
    $(UUR_MOD_DIR)/src/types/psbt.c \
    $(UUR_MOD_DIR)/src/types/registry.c

CFLAGS_USERMOD += -I$(UUR_MOD_DIR) -I$(UUR_MOD_DIR)/src
