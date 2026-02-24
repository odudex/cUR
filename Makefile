# Root Makefile for UR C implementation testing

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
INCLUDES = -Isrc
SRCDIR = src
OBJDIR = src/obj

# Source files (exclude test files)
SOURCES = utils.c bytewords.c fountain_decoder.c fountain_encoder.c fountain_utils.c crc32.c ur_decoder.c ur_encoder.c ur.c sha256/sha256.c \
          types/byte_buffer.c types/cbor_data.c types/cbor_encoder.c types/cbor_decoder.c types/registry.c types/bytes_type.c types/psbt.c types/bip39.c \
          types/keypath.c types/hd_key.c types/multi_key.c types/output.c

# Object files
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)

# Target library
TARGET = $(SRCDIR)/libur.a

# Test utilities
TEST_UTILS_SOURCES = tests/test_utils.c
TEST_UTILS_OBJECT = tests/test_utils.o

# Test programs
# Cross-implementation testing
TEST_PRINT_FRAGMENTS_TARGET = tests/cross_implementation_tests/test_print_fragments
TEST_PRINT_FRAGMENTS_SOURCES = tests/cross_implementation_tests/test_print_fragments.c

TEST_PRINT_PSBT_FRAGMENTS_TARGET = tests/cross_implementation_tests/test_print_psbt_fragments
TEST_PRINT_PSBT_FRAGMENTS_SOURCES = tests/cross_implementation_tests/test_print_psbt_fragments.c

TEST_BYTES_DECODER_TARGET = tests/test_ur_bytes_decoder
TEST_BYTES_DECODER_SOURCES = tests/test_ur_bytes_decoder.c

TEST_BYTES_ENCODER_TARGET = tests/test_ur_bytes_encoder
TEST_BYTES_ENCODER_SOURCES = tests/test_ur_bytes_encoder.c

TEST_OUTPUT_DECODER_TARGET = tests/test_ur_output_decoder
TEST_OUTPUT_DECODER_SOURCES = tests/test_ur_output_decoder.c

TEST_OUTPUT_ENCODER_TARGET = tests/test_ur_output_encoder
TEST_OUTPUT_ENCODER_SOURCES = tests/test_ur_output_encoder.c

TEST_PSBT_DECODER_TARGET = tests/test_ur_PSBT_decoder
TEST_PSBT_DECODER_SOURCES = tests/test_ur_PSBT_decoder.c

TEST_PSBT_ENCODER_TARGET = tests/test_ur_PSBT_encoder
TEST_PSBT_ENCODER_SOURCES = tests/test_ur_PSBT_encoder.c

TEST_BIP39_DECODER_TARGET = tests/test_ur_bip39_decoder
TEST_BIP39_DECODER_SOURCES = tests/test_ur_bip39_decoder.c

TEST_ACCOUNT_DESCRIPTOR_DECODER_TARGET = tests/test_ur_account_descriptor_decoder
TEST_ACCOUNT_DESCRIPTOR_DECODER_SOURCES = tests/test_ur_account_descriptor_decoder.c

TEST_OUTPUT_DESCRIPTOR_ROUNDTRIP_TARGET = tests/test_ur_output_descriptor_roundtrip
TEST_OUTPUT_DESCRIPTOR_ROUNDTRIP_SOURCES = tests/test_ur_output_descriptor_roundtrip.c

.PHONY: all clean test-print-fragments test-print-psbt-fragments test-bytes-decoder test-bytes-encoder test-output-decoder test-output-encoder test-PSBT-decoder test-PSBT-encoder test-bip39-decoder test-account-descriptor-decoder test-output-descriptor-roundtrip

all: $(TARGET)

$(TARGET): $(OBJECTS)
	ar rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test utilities
$(TEST_UTILS_OBJECT): $(TEST_UTILS_SOURCES) tests/test_utils.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build fragment printer for cross-implementation testing
test-print-fragments: $(TARGET) $(TEST_PRINT_FRAGMENTS_TARGET)

$(TEST_PRINT_FRAGMENTS_TARGET): $(TEST_PRINT_FRAGMENTS_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

# Build PSBT fragment printer for cross-implementation testing
test-print-psbt-fragments: $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_PRINT_PSBT_FRAGMENTS_TARGET)

$(TEST_PRINT_PSBT_FRAGMENTS_TARGET): $(TEST_PRINT_PSBT_FRAGMENTS_SOURCES) $(TEST_UTILS_OBJECT) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_UTILS_OBJECT) -L$(SRCDIR) -lur -lm -o $@

# Build and run Bytes decoder test
test-bytes-decoder: $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_BYTES_DECODER_TARGET)
	./$(TEST_BYTES_DECODER_TARGET)

$(TEST_BYTES_DECODER_TARGET): $(TEST_BYTES_DECODER_SOURCES) $(TEST_UTILS_OBJECT) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_UTILS_OBJECT) -L$(SRCDIR) -lur -lm -o $@

# Build and run Bytes encoder test
test-bytes-encoder: $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_BYTES_ENCODER_TARGET)
	./$(TEST_BYTES_ENCODER_TARGET)

$(TEST_BYTES_ENCODER_TARGET): $(TEST_BYTES_ENCODER_SOURCES) $(TEST_UTILS_OBJECT) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_UTILS_OBJECT) -L$(SRCDIR) -lur -lm -o $@

# Build and run Output decoder test
test-output-decoder: $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_OUTPUT_DECODER_TARGET)
	./$(TEST_OUTPUT_DECODER_TARGET)

$(TEST_OUTPUT_DECODER_TARGET): $(TEST_OUTPUT_DECODER_SOURCES) $(TEST_UTILS_OBJECT) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_UTILS_OBJECT) -L$(SRCDIR) -lur -lm -o $@

# Build and run Output encoder test
test-output-encoder: $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_OUTPUT_ENCODER_TARGET)
	./$(TEST_OUTPUT_ENCODER_TARGET)

$(TEST_OUTPUT_ENCODER_TARGET): $(TEST_OUTPUT_ENCODER_SOURCES) $(TEST_UTILS_OBJECT) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_UTILS_OBJECT) -L$(SRCDIR) -lur -lm -o $@

# Build and run PSBT decoder test
test-PSBT-decoder: $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_PSBT_DECODER_TARGET)
	./$(TEST_PSBT_DECODER_TARGET)

$(TEST_PSBT_DECODER_TARGET): $(TEST_PSBT_DECODER_SOURCES) $(TEST_UTILS_OBJECT) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_UTILS_OBJECT) -L$(SRCDIR) -lur -lm -o $@

# Build and run PSBT encoder test
test-PSBT-encoder: $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_PSBT_ENCODER_TARGET)
	./$(TEST_PSBT_ENCODER_TARGET)

$(TEST_PSBT_ENCODER_TARGET): $(TEST_PSBT_ENCODER_SOURCES) $(TEST_UTILS_OBJECT) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_UTILS_OBJECT) -L$(SRCDIR) -lur -lm -o $@

# Build and run BIP39 decoder test
test-bip39-decoder: $(TARGET) $(TEST_BIP39_DECODER_TARGET)
	./$(TEST_BIP39_DECODER_TARGET)

$(TEST_BIP39_DECODER_TARGET): $(TEST_BIP39_DECODER_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

# Build and run Account Descriptor decoder test
test-account-descriptor-decoder: $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_ACCOUNT_DESCRIPTOR_DECODER_TARGET)
	./$(TEST_ACCOUNT_DESCRIPTOR_DECODER_TARGET)

$(TEST_ACCOUNT_DESCRIPTOR_DECODER_TARGET): $(TEST_ACCOUNT_DESCRIPTOR_DECODER_SOURCES) $(TEST_UTILS_OBJECT) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_UTILS_OBJECT) -L$(SRCDIR) -lur -lm -o $@

# Build and run Output descriptor roundtrip test
test-output-descriptor-roundtrip: $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_OUTPUT_DESCRIPTOR_ROUNDTRIP_TARGET)
	./$(TEST_OUTPUT_DESCRIPTOR_ROUNDTRIP_TARGET)

$(TEST_OUTPUT_DESCRIPTOR_ROUNDTRIP_TARGET): $(TEST_OUTPUT_DESCRIPTOR_ROUNDTRIP_SOURCES) $(TEST_UTILS_OBJECT) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TEST_UTILS_OBJECT) -L$(SRCDIR) -lur -lm -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET) $(TEST_UTILS_OBJECT) $(TEST_PRINT_FRAGMENTS_TARGET) $(TEST_PRINT_PSBT_FRAGMENTS_TARGET) $(TEST_BYTES_DECODER_TARGET) $(TEST_BYTES_ENCODER_TARGET) $(TEST_OUTPUT_DECODER_TARGET) $(TEST_OUTPUT_ENCODER_TARGET) $(TEST_PSBT_DECODER_TARGET) $(TEST_PSBT_ENCODER_TARGET) $(TEST_BIP39_DECODER_TARGET) $(TEST_ACCOUNT_DESCRIPTOR_DECODER_TARGET) $(TEST_OUTPUT_DESCRIPTOR_ROUNDTRIP_TARGET) tests/cross_implementation_tests/test_cross_output

# Dependencies
$(OBJDIR)/utils.o: $(SRCDIR)/utils.c $(SRCDIR)/utils.h
$(OBJDIR)/crc32.o: $(SRCDIR)/crc32.c $(SRCDIR)/crc32.h
$(OBJDIR)/bytewords.o: $(SRCDIR)/bytewords.c $(SRCDIR)/bytewords.h $(SRCDIR)/utils.h $(SRCDIR)/crc32.h
$(OBJDIR)/fountain_utils.o: $(SRCDIR)/fountain_utils.c $(SRCDIR)/fountain_utils.h $(SRCDIR)/fountain_decoder.h $(SRCDIR)/fountain_types.h $(SRCDIR)/utils.h $(SRCDIR)/sha256/sha256.h
$(OBJDIR)/fountain_decoder.o: $(SRCDIR)/fountain_decoder.c $(SRCDIR)/fountain_decoder.h $(SRCDIR)/fountain_utils.h $(SRCDIR)/fountain_types.h $(SRCDIR)/crc32.h $(SRCDIR)/utils.h
$(OBJDIR)/fountain_encoder.o: $(SRCDIR)/fountain_encoder.c $(SRCDIR)/fountain_encoder.h $(SRCDIR)/fountain_decoder.h $(SRCDIR)/fountain_utils.h $(SRCDIR)/fountain_types.h $(SRCDIR)/crc32.h $(SRCDIR)/utils.h
$(OBJDIR)/types/byte_buffer.o: $(SRCDIR)/types/byte_buffer.c $(SRCDIR)/types/byte_buffer.h $(SRCDIR)/sha256/sha256.h $(SRCDIR)/utils.h
$(OBJDIR)/ur_decoder.o: $(SRCDIR)/ur_decoder.c $(SRCDIR)/ur_decoder.h $(SRCDIR)/fountain_decoder.h $(SRCDIR)/bytewords.h $(SRCDIR)/utils.h
$(OBJDIR)/ur_encoder.o: $(SRCDIR)/ur_encoder.c $(SRCDIR)/ur_encoder.h $(SRCDIR)/fountain_encoder.h $(SRCDIR)/bytewords.h $(SRCDIR)/utils.h
$(OBJDIR)/ur.o: $(SRCDIR)/ur.c $(SRCDIR)/ur.h $(SRCDIR)/ur_decoder.h $(SRCDIR)/utils.h
$(OBJDIR)/sha256/sha256.o: $(SRCDIR)/sha256/sha256.c $(SRCDIR)/sha256/sha256.h