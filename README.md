# C Implementation of Blockchain Commons Uniform Resources

## Overview

This project aims to provide a lightweight, high-performance C implementation of the [Blockchain Commons Uniform Resources (UR)](https://github.com/BlockchainCommons/Research/blob/master/papers/bcr-2020-005-ur.md) specification. URs are a method for encoding structured binary data in a format optimized for transport via QR codes and other data channels.

## MicroPython Integration

This library can be integrated directly into C applications. For MicroPython-based embedded applications, integration is possible through a C wrapper module like the `uUR.c` example.

## License

This project is licensed under the **BSD-2-Clause Plus Patent License**.

Copyright © 2025 Krux Contributors

This is an independent implementation written using foundation-ur-py as a reference for testing and validation. The implementation follows the Blockchain Commons UR specification but does not derive from their codebase.

See the [LICENSE](LICENSE) file for full license text.

## Building

Build the UR library:

```bash
make clean && make all
```

This creates `src/libur.a` with both UREncoder and URDecoder implementations.

## Testing

All test files are organized in the `tests/` directory.

### URDecoder Tests

Test the decoder with pre-generated UR fragments:

```bash
make test-decoder
```

This runs `tests/test_ur_decoder` which:
- Reads UR fragments from `tests/test_cases/*.UR_fragments.txt`
- Decodes them with URDecoder
- Verifies against original CBOR data in `tests/test_cases/*.UR_object.txt`

Run decoder test for a specific test case:

```bash
./tests/test_ur_decoder tests/test_cases/data_1.UR_fragments.txt
```

### UREncoder Tests

Test the encoder with encode/decode roundtrip verification:

```bash
make test-encoder
```

This runs `tests/test_ur_encoder` which:
- Reads CBOR data from `tests/test_cases/*.UR_object.txt`
- Encodes with UREncoder to generate UR fragments
- Decodes fragments with URDecoder
- Verifies perfect roundtrip (original data matches decoded data)

### Test Results

All test suites pass with 100% success rate on all 5 test cases:
- ✅ URDecoder: 5/5 tests passing
- ✅ UREncoder: 5/5 tests passing (encode → decode roundtrip verified)