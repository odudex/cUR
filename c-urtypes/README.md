# bc-urtypes - C Implementation

Pure C implementation of Blockchain Commons UR Types for CBOR encoding/decoding of cryptocurrency data structures.

## Implemented Types

- **Bytes** - Simple byte wrapper
- **PSBT** - Partially Signed Bitcoin Transaction
- **BIP39** - Mnemonic seed phrases
- **Output** - Bitcoin output descriptors
- **Account** - HD wallet account structure (helper function)

## Usage

```c
#include "psbt.h"

// Decode PSBT from CBOR
psbt_data_t *psbt = psbt_from_cbor(cbor_data, cbor_len);

// Access data
size_t len;
const uint8_t *data = psbt_get_data(psbt, &len);

// Cleanup
psbt_free(psbt);
```

## Building

```bash
make              # Build all tests
make test         # Run tests
make clean        # Clean build artifacts
```

## MicroPython

`uURTypes.c` provides MicroPython bindings with simple module functions:

**Bytes Functions:**
- `uURTypes.bytes_from_cbor(cbor)` - Decode Bytes from CBOR, returns raw bytes
- `uURTypes.bytes_to_cbor(data)` - Encode raw bytes to CBOR

**PSBT Functions:**
- `uURTypes.psbt_from_cbor(cbor)` - Decode PSBT from CBOR, returns raw PSBT bytes
- `uURTypes.psbt_to_cbor(psbt_bytes)` - Encode raw PSBT bytes to CBOR

**Output Functions:**
- `uURTypes.output_from_cbor(cbor)` - Parse output descriptor, returns descriptor string
- `uURTypes.output_from_cbor_account(cbor)` - Extract first output from account

**BIP39 (namespace):**
- `uURTypes.BIP39.words_from_cbor(cbor)` - Decode BIP39 from CBOR, returns word list

**Constants:**
- Type constants: `CRYPTO_PSBT_TYPE = "crypto-psbt"` (with hyphen)
- Tag constants: `CRYPTO_PSBT_TAG`, `CRYPTO_BIP39_TAG`, etc.

## License

MIT License
