# Makefile for urtypes tests

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../src -g
LDFLAGS = -lm

# Source directories
SRC_DIR = ../src
TEST_DIR = .

# Source files from the main implementation
SRCS = $(SRC_DIR)/bytes_type.c \
       $(SRC_DIR)/psbt.c \
       $(SRC_DIR)/bip39.c \
       $(SRC_DIR)/registry.c \
       $(SRC_DIR)/cbor_data.c \
       $(SRC_DIR)/cbor_encoder.c \
       $(SRC_DIR)/cbor_decoder.c \
       $(SRC_DIR)/utils.c

# Test files
TEST_SRCS = test_bytes.c test_psbt.c test_bip39.c

# Test executables
TEST_BINS = test_bytes test_psbt test_bip39

# Object files for the main implementation
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TEST_BINS)

# Build test executables
test_bytes: test_bytes.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS) $(LDFLAGS)

test_psbt: test_psbt.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS) $(LDFLAGS)

test_bip39: test_bip39.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS) $(LDFLAGS)

# Compile source files to object files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Run all tests
test: $(TEST_BINS)
	@echo ""
	@echo "======================================"
	@echo "Running all urtypes tests"
	@echo "======================================"
	@for test in $(TEST_BINS); do \
		echo ""; \
		./$$test || exit 1; \
	done
	@echo ""
	@echo "======================================"
	@echo "All tests completed!"
	@echo "======================================"

# Run individual tests
run-bytes: test_bytes
	./test_bytes

run-psbt: test_psbt
	./test_psbt

run-bip39: test_bip39
	./test_bip39

# Clean build artifacts
clean:
	rm -f $(TEST_BINS)
	rm -f $(OBJS)
	rm -f *.o

# Help
help:
	@echo "urtypes Test Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build all test executables (default)"
	@echo "  test        - Build and run all tests"
	@echo "  run-bytes   - Run Bytes tests only"
	@echo "  run-psbt    - Run PSBT tests only"
	@echo "  run-bip39   - Run BIP39 tests only"
	@echo "  clean       - Remove all build artifacts"
	@echo "  help        - Show this help message"

.PHONY: all test run-bytes run-psbt run-bip39 clean help
