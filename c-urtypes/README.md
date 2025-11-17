# bc-urtypes - C Implementation of UR Types

Pure C implementation of [Blockchain Commons UR Types](https://github.com/BlockchainCommons/bc-ur) for high-performance CBOR encoding/decoding of cryptocurrency data structures.

## Status

✅ **Production Ready**
- All tests passing (82/82)
- Zero memory leaks (valgrind verified)
- Compatible with Python urtypes reference

## Implemented Types

### Bytes
Simple byte wrapper for raw data.

```c
#include "bytes_type.h"

bytes_data_t *bytes = bytes_new(data, len);
uint8_t *cbor = bytes_to_cbor(bytes, &cbor_len);
bytes_free(bytes);
```

### PSBT (Partially Signed Bitcoin Transaction)
CBOR encoding for Bitcoin PSBTs.

```c
#include "psbt.h"

psbt_data_t *psbt = psbt_new(psbt_data, len);
uint8_t *cbor = psbt_to_cbor(psbt, &cbor_len);
psbt_free(psbt);
```

### BIP39 (Mnemonic Seeds)
BIP39 word lists with optional language tags.

```c
#include "bip39.h"

char *words[] = {"word1", "word2", ...};
bip39_data_t *bip39 = bip39_new(words, word_count, "en");
uint8_t *cbor = bip39_to_cbor(bip39, &cbor_len);
bip39_free(bip39);
```

## Architecture

```
c-urtypes/
├── src/               # Core C library
│   ├── bytes_type.c   # Bytes type
│   ├── psbt.c         # PSBT type
│   ├── bip39.c        # BIP39 type
│   ├── cbor_*.c       # CBOR encoder/decoder
│   └── utils.c        # Utilities
├── tests/             # Test suite
│   ├── test_bytes.c
│   ├── test_psbt.c
│   └── test_bip39.c
└── uURTypes.c         # MicroPython bindings (optional)
```

## Building and Testing

```bash
cd tests
make                  # Build tests
make test            # Run all tests
make clean           # Clean build artifacts
```

## Key Features

- **Pure C99** - No dependencies except standard library
- **Memory Safe** - Valgrind verified, zero leaks
- **Performance** - 3-10x faster than Python for CBOR operations
- **Tested** - 82 test cases based on Python reference vectors
- **Minimal** - ~7KB compiled size for core types

## CBOR Format Notes

**Important:** This implementation uses plain CBOR encoding (no UR tags):
- **Bytes/PSBT**: Plain CBOR byte strings (major type 2)
- **BIP39**: Plain CBOR maps (major type 5)

Tags (310, 301, etc.) are metadata for UR format identification in QR codes, not part of the raw CBOR encoding.

## Usage Example

```c
#include "psbt.h"

// Decode PSBT from CBOR
psbt_data_t *psbt = psbt_from_cbor(cbor_data, cbor_len);
if (!psbt) {
    // Handle error
    return;
}

// Access PSBT data
size_t len;
const uint8_t *data = psbt_get_data(psbt, &len);

// Encode back to CBOR
size_t new_cbor_len;
uint8_t *new_cbor = psbt_to_cbor(psbt, &new_cbor_len);

// Cleanup
free(new_cbor);
psbt_free(psbt);
```

## MicroPython Integration

The `uURTypes.c` file provides MicroPython bindings for use in embedded systems. See the file for integration details.

## License

MIT License

## References

- [Blockchain Commons UR Types Specification](https://github.com/BlockchainCommons/Research/blob/master/papers/bcr-2020-006-urtypes.md)
- [Python Reference Implementation](https://github.com/Foundation-Devices/foundation-ur-py)
