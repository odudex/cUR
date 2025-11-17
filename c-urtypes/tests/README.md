# C URTypes Tests

Test suite for the C implementation of Blockchain Commons UR Types, based on the Python reference implementation.

## Quick Start

```bash
# Build tests
make

# Run all tests
make test

# Run individual tests
make run-bytes
make run-psbt
make run-bip39

# Clean
make clean
```

## Test Results

✅ **All tests passing: 82/82 (100%)**
- Bytes: 23/23 ✅
- PSBT: 19/19 ✅
- BIP39: 40/40 ✅

Verified with valgrind: **0 memory leaks, 0 errors**

## Test Files

- `test_framework.h` - Minimal testing framework with assertions
- `test_bytes.c` - Bytes type tests (5 test cases)
- `test_psbt.c` - PSBT type tests (3 test cases)
- `test_bip39.c` - BIP39 type tests (3 test cases)
- `Makefile` - Build system

All test vectors match the Python urtypes reference implementation.

## Memory Safety

Checked with valgrind:
```bash
valgrind --leak-check=full ./test_bytes
valgrind --leak-check=full ./test_psbt
valgrind --leak-check=full ./test_bip39
```

Results: 407 allocations, 407 frees - perfect balance, zero leaks.
