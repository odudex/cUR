# Makefile for c-urtypes with Output support

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Isrc -g
LDFLAGS = -lm

# Source files
SRCS = src/utils.c \
       src/cbor_data.c \
       src/cbor_encoder.c \
       src/cbor_decoder.c \
       src/registry.c \
       src/bytes_type.c \
       src/psbt.c \
       src/bip39.c \
       src/sha256.c \
       src/keypath.c \
       src/ec_key.c \
       src/hd_key.c \
       src/multi_key.c \
       src/output.c

# Object files
OBJS = $(SRCS:.c=.o)

# Test executables
TEST_BYTES = tests/test_bytes
TEST_PSBT = tests/test_psbt
TEST_BIP39 = tests/test_bip39
TEST_OUTPUT = tests/test_output
TEST_ACCOUNT = tests/test_account

.PHONY: all clean test test_output test_account

all: $(TEST_BYTES) $(TEST_PSBT) $(TEST_BIP39) $(TEST_OUTPUT) $(TEST_ACCOUNT)

# Compile object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build test executables
$(TEST_BYTES): tests/test_bytes.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_PSBT): tests/test_psbt.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_BIP39): tests/test_bip39.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_OUTPUT): tests/test_output.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_ACCOUNT): tests/test_account.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Run all tests
test: all
	@echo "=== Running Bytes tests ==="
	./$(TEST_BYTES)
	@echo "\n=== Running PSBT tests ==="
	./$(TEST_PSBT)
	@echo "\n=== Running BIP39 tests ==="
	./$(TEST_BIP39)

# Run Output tests specifically
test_output: $(TEST_OUTPUT)
	@echo "=== Running Output tests ==="
	./$(TEST_OUTPUT)

# Run Account tests specifically
test_account: $(TEST_ACCOUNT)
	@echo "=== Running Account tests ==="
	./$(TEST_ACCOUNT)

# Clean build artifacts
clean:
	rm -f $(OBJS)
	rm -f $(TEST_BYTES) $(TEST_PSBT) $(TEST_BIP39) $(TEST_OUTPUT) $(TEST_ACCOUNT)

# Valgrind memory check
valgrind: $(TEST_OUTPUT)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TEST_OUTPUT)
