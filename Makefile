# Root Makefile for UR C implementation testing

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
INCLUDES = -Isrc
SRCDIR = src
OBJDIR = src/obj

# Source files (exclude test files)
SOURCES = utils.c bytewords.c cbor_lite.c fountain_decoder.c fountain_encoder.c fountain_utils.c crc32.c ur_decoder.c ur_encoder.c ur.c sha256/sha256.c

# Object files
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)

# Target library
TARGET = $(SRCDIR)/libur.a

# Test programs
TEST_DECODER_TARGET = tests/test_ur_decoder
TEST_DECODER_SOURCES = tests/test_ur_decoder.c

TEST_ENCODER_TARGET = tests/test_ur_encoder
TEST_ENCODER_SOURCES = tests/test_ur_encoder.c

TEST_PRINT_FRAGMENTS_TARGET = tests/test_print_fragments
TEST_PRINT_FRAGMENTS_SOURCES = tests/test_print_fragments.c

.PHONY: all clean test-decoder test-encoder test-print-fragments

all: $(TARGET)

$(TARGET): $(OBJECTS)
	ar rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build and run decoder test
test-decoder: $(TARGET) $(TEST_DECODER_TARGET)
	./$(TEST_DECODER_TARGET)

$(TEST_DECODER_TARGET): $(TEST_DECODER_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

# Build and run encoder test
test-encoder: $(TARGET) $(TEST_ENCODER_TARGET)
	./$(TEST_ENCODER_TARGET)

$(TEST_ENCODER_TARGET): $(TEST_ENCODER_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

# Build fragment printer for cross-implementation testing
test-print-fragments: $(TARGET) $(TEST_PRINT_FRAGMENTS_TARGET)

$(TEST_PRINT_FRAGMENTS_TARGET): $(TEST_PRINT_FRAGMENTS_SOURCES) $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(SRCDIR) -lur -lm -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET) $(TEST_DECODER_TARGET) $(TEST_ENCODER_TARGET) $(TEST_PRINT_FRAGMENTS_TARGET) tests/test_cross_output

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