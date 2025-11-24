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

`uURTypes.c` provides MicroPython bindings:
- `uURTypes.PSBT`, `uURTypes.Bytes`, `uURTypes.BIP39`
- `uURTypes.output_from_cbor(cbor)` - Parse output descriptor
- `uURTypes.output_from_cbor_account(cbor)` - Extract first output from account
- Type constants: `CRYPTO_PSBT_TYPE = "crypto-psbt"` (with hyphen)

## License

MIT License
