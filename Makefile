# Root Makefile for UR C implementation testing

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
INCLUDES = -Isrc
SRCDIR = src
OBJDIR = src/obj

# Source files (exclude test files)
SOURCES = utils.c bytewords.c cbor_lite.c fountain_decoder.c fountain_encoder.c fountain_utils.c crc32.c ur_decoder.c ur_encoder.c ur.c sha256/sha256.c \
          types/utils.c types/cbor_data.c types/cbor_encoder.c types/cbor_decoder.c types/registry.c types/bytes_type.c types/psbt.c types/bip39.c \
          types/keypath.c types/ec_key.c types/hd_key.c types/multi_key.c types/output.c

# Object files
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)

# Target library
TARGET = $(SRCDIR)/libur.a

# Test programs
TEST_PRINT_FRAGMENTS_TARGET = tests/test_print_fragments
TEST_PRINT_FRAGMENTS_SOURCES = tests/test_print_fragments.c

TEST_BYTES_TARGET = tests/test_bytes
TEST_BYTES_SOURCES = tests/test_bytes.c

TEST_OUTPUT_TARGET = tests/test_output
TEST_OUTPUT_SOURCES = tests/test_output.c

TEST_PSBT_DECODER_TARGET = tests/test_ur_PSBT_decoder
TEST_PSBT_DECODER_SOURCES = tests/test_ur_PSBT_decoder.c

TEST_PSBT_ENCODER_TARGET = tests/test_ur_PSBT_encoder
TEST_PSBT_ENCODER_SOURCES = tests/test_ur_PSBT_encoder.c

TEST_BIP39_DECODER_TARGET = tests/test_ur_bip39_decoder
TEST_BIP39_DECODER_SOURCES = tests/test_ur_bip39_decoder.c

.PHONY: all clean test-print-fragments test-bytes test-output test-PSBT-decoder test-PSBT-encoder test-bip39-decoder

all: $(TARGET)

$(TARGET): $(OBJECTS)
	ar rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build fragment printer for cross-implementation testing
test-print-fragments: $(TARGET) $(TEST_PRINT_FRAGMENTS_TARGET)

$(TEST_PRINT_FRAGMENTS_TARGET): $(TEST_PRINT_FRAGMENTS_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

# Build and run bytes test
test-bytes: $(TARGET) $(TEST_BYTES_TARGET)
	./$(TEST_BYTES_TARGET)

$(TEST_BYTES_TARGET): $(TEST_BYTES_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

# Build and run Output test
test-output: $(TARGET) $(TEST_OUTPUT_TARGET)
	./$(TEST_OUTPUT_TARGET)

$(TEST_OUTPUT_TARGET): $(TEST_OUTPUT_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

# Build and run PSBT decoder test
test-PSBT-decoder: $(TARGET) $(TEST_PSBT_DECODER_TARGET)
	./$(TEST_PSBT_DECODER_TARGET)

$(TEST_PSBT_DECODER_TARGET): $(TEST_PSBT_DECODER_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

# Build and run PSBT encoder test
test-PSBT-encoder: $(TARGET) $(TEST_PSBT_ENCODER_TARGET)
	./$(TEST_PSBT_ENCODER_TARGET)

$(TEST_PSBT_ENCODER_TARGET): $(TEST_PSBT_ENCODER_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

# Build and run BIP39 decoder test
test-bip39-decoder: $(TARGET) $(TEST_BIP39_DECODER_TARGET)
	./$(TEST_BIP39_DECODER_TARGET)

$(TEST_BIP39_DECODER_TARGET): $(TEST_BIP39_DECODER_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET) $(TEST_PRINT_FRAGMENTS_TARGET) $(TEST_BYTES_TARGET) $(TEST_OUTPUT_TARGET) $(TEST_PSBT_DECODER_TARGET) $(TEST_PSBT_ENCODER_TARGET) $(TEST_BIP39_DECODER_TARGET) tests/test_cross_output

# Dependencies
$(OBJDIR)/utils.o: $(SRCDIR)/utils.c $(SRCDIR)/utils.h
$(OBJDIR)/crc32.o: $(SRCDIR)/crc32.c $(SRCDIR)/crc32.h
$(OBJDIR)/cbor_lite.o: $(SRCDIR)/cbor_lite.c $(SRCDIR)/cbor_lite.h
$(OBJDIR)/bytewords.o: $(SRCDIR)/bytewords.c $(SRCDIR)/bytewords.h $(SRCDIR)/utils.h $(SRCDIR)/crc32.h
$(OBJDIR)/fountain_utils.o: $(SRCDIR)/fountain_utils.c $(SRCDIR)/fountain_utils.h $(SRCDIR)/fountain_decoder.h $(SRCDIR)/utils.h $(SRCDIR)/sha256/sha256.h
$(OBJDIR)/fountain_decoder.o: $(SRCDIR)/fountain_decoder.c $(SRCDIR)/fountain_decoder.h $(SRCDIR)/fountain_utils.h $(SRCDIR)/crc32.h $(SRCDIR)/utils.h
$(OBJDIR)/fountain_encoder.o: $(SRCDIR)/fountain_encoder.c $(SRCDIR)/fountain_encoder.h $(SRCDIR)/fountain_decoder.h $(SRCDIR)/fountain_utils.h $(SRCDIR)/cbor_lite.h $(SRCDIR)/crc32.h $(SRCDIR)/utils.h
$(OBJDIR)/ur_decoder.o: $(SRCDIR)/ur_decoder.c $(SRCDIR)/ur_decoder.h $(SRCDIR)/fountain_decoder.h $(SRCDIR)/bytewords.h $(SRCDIR)/utils.h
$(OBJDIR)/ur_encoder.o: $(SRCDIR)/ur_encoder.c $(SRCDIR)/ur_encoder.h $(SRCDIR)/fountain_encoder.h $(SRCDIR)/bytewords.h $(SRCDIR)/utils.h
$(OBJDIR)/ur.o: $(SRCDIR)/ur.c $(SRCDIR)/ur.h $(SRCDIR)/ur_decoder.h $(SRCDIR)/utils.h
$(OBJDIR)/sha256/sha256.o: $(SRCDIR)/sha256/sha256.c $(SRCDIR)/sha256/sha256.h