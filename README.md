# cUR — C implementation of Blockchain Commons Uniform Resources

A small, reviewable C implementation of the [Blockchain Commons Uniform Resources (UR)](https://github.com/BlockchainCommons/Research/blob/master/papers/bcr-2020-005-ur.md)
specification, which encodes binary data for transport in QR codes.
One source tree builds for PC, ESP-IDF, MicroPython and CPython.

## Supported UR types

| UR type    | CBOR tag | Source                            |
|------------|---------:|-----------------------------------|
| `bytes`    | —        | `src/types/bytes_type.c`          |
| `psbt`     | 310      | `src/types/psbt.c`                |
| `bip39`    | —        | `src/types/bip39.c`               |
| `hd-key`   | 303      | `src/types/hd_key.c`              |
| `output`   | 308      | `src/types/output.c`              |
| `account`  | 311      | `src/types/output.c` + `multi_key.c` |

Helper types live alongside: `keypath.c` (BIP-32 derivation paths) and
`multi_key.c` (multisig keys).

Not supported: the version 2 descriptor types (`output-descriptor`
40308, `hdkey` 40303, `keypath` 40304, `coin-info` 40305 and
`account-descriptor`), and keypath range or pair components such as
`<0;1>/*`. Multipath, miniscript and `tr()` script-tree descriptors
therefore cannot be carried in a UR; send them as descriptor text. Input
that uses a range or pair component is rejected, not truncated.

## Building (PC)

```bash
make clean && make all        # produces src/libur.a
make test                     # runs every test suite
make check                    # alias for `make test`
```

Sanitizer build — swaps `-O2` for `-O0 -fsanitize=address,undefined`:

```bash
make clean && make DEBUG=1 test
```

Coverage report (requires `lcov`):

```bash
make coverage                 # writes coverage_html/index.html
```

Individual suites are also addressable (`make test-bytes-decoder`,
`make test-negative`, etc.).

## Minimal API example

Encode → decode roundtrip:

```c
#include "ur_encoder.h"
#include "ur_decoder.h"

// Your CBOR payload (produced via types/*.c or your own encoder).
const uint8_t cbor[] = { /* ... */ };
const size_t  cbor_len = sizeof(cbor);

// Encoder: type name, payload, max/min fragment bytes, starting seq num.
ur_encoder_t *enc = ur_encoder_new("bytes", cbor, cbor_len,
                                   /*max_fragment_len=*/200,
                                   /*first_seq_num=*/0,
                                   /*min_fragment_len=*/10);

ur_decoder_t *dec = ur_decoder_new();
char *part = NULL;
ur_decoder_state_t state = UR_DECODER_PROCESSING;
while (!ur_decoder_state_is_terminal(state)) {
    if (!ur_encoder_next_part(enc, &part)) break;
    state = ur_decoder_receive_part(dec, part);
    free(part);
}

if (state == UR_DECODER_OK) {
    ur_result_t *r = ur_decoder_get_result(dec); /* guaranteed non-NULL */
    // r->type, r->cbor_data, r->cbor_len
}

ur_decoder_free(dec);
ur_encoder_free(enc);
```

The decoder is a state machine: `ur_decoder_receive_part()` returns the
state after each part, and `ur_decoder_get_state()` reads it without
feeding one. `UR_DECODER_PROCESSING` means keep feeding parts. The
terminal states (`UR_DECODER_OK`, `UR_DECODER_NO_RESULT`,
`UR_DECODER_ERROR_INVALID_CHECKSUM`) are permanent; any other error only
rejects that frame, and scanning can continue.

API rules:

- `UR_DECODER_OK` is 0, so the return value is not a success flag.
  Compare it with the state constants or use
  `ur_decoder_state_is_error()` / `ur_decoder_state_is_terminal()`.
- The progress estimates (`ur_decoder_estimated_percent_complete*()` and
  the `fountain_decoder_*` equivalents) return `float`. Fragment
  selection uses `double`, as the reference implementations do.
- `ur_new()`, `ur_encoder_new()` and `ur_encoder_encode_single()` reject
  a `type` that is not a valid UR type (`[a-z0-9-]+`, no leading or
  trailing `-`). `fountain_encoder_new()` rejects
  `max_fragment_len < 10`.
- `output_descriptor_from_cbor_account()` returns the first output of a
  `crypto-account`; `output_descriptors_from_cbor_account()` returns all
  of them (NULL for an output that could not be decoded), freed with
  `output_descriptors_free()`. Both require the account's master
  fingerprint and apply it to origins that lack one.

Type-specific helpers (`bytes_from_cbor`, `psbt_from_cbor`,
`output_from_descriptor_string`, etc.) live in `src/types/*.h`.

## Build options

All options default to the smallest, simplest implementation; the
performance variants are opt-in (or, for the P4 vector path, gated by
the CPU with an opt-out). Each option is a single flag with the same
name everywhere it appears — Makefile variable, CMake option, and
ESP-IDF Kconfig.

| Option | Default | Effect |
|--------|---------|--------|
| `UR_CRC32_SLICE_BY_8` | off | Slice-by-8 CRC32: about 2.7x faster on ESP32-P4, +8 KB flash for a const table. |
| `UR_XOR_ESP32P4_SIMD` | on (ESP32-P4 only) | PIE 128-bit vector XOR for fountain-code mixing, with transparent word-wise fallback on unaligned data. Only exists on ESP32-P4; Kconfig opt-out. |
| `UR_ALLOC_PSRAM` | on (ESP targets) | Route the library's buffers to PSRAM with internal-RAM fallback, keeping fountain-decoder churn out of scarce internal heap. Kconfig opt-out; no-op elsewhere. |
| `UR_ENVELOPE_ONLY` | off | CMake: build only the UR transport layer (bytewords, fountain, multi-part assembly), excluding the `src/types/` payload codecs, for integrators that do their own CBOR. |

## Platform integration

The SHA-256 dependency is the only platform-sensitive piece. A
compile-time flag picks the backend; see `src/sha256/sha256_compat.h`.

| Target           | Flag                    | Backend                       |
|------------------|-------------------------|-------------------------------|
| PC (default)     | *(none)*                | bundled `src/sha256/sha256.c` |
| ESP-IDF          | `UR_USE_MBEDTLS_SHA256` | mbedTLS PSA (HW-accelerated)  |
| K210 (legacy)    | `UR_USE_K210_SHA256`    | `sha256_hard_calculate` (HW)  |

### ESP-IDF component

Add this repository as a component (e.g. under `components/cUR/` or as
a submodule). The bundled `CMakeLists.txt` registers it with
`idf_component_register()` and sets `UR_USE_MBEDTLS_SHA256`:

```cmake
# components/cUR/CMakeLists.txt is already provided.
# In your app's main/CMakeLists.txt just list the component as a dep:
idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "." REQUIRES cUR)
```

Then include headers normally: `#include "ur_encoder.h"`.

### Legacy K210 port

Maintained for compatibility only. The build wiring lives in the
consuming project, which must pass `-DUR_USE_K210_SHA256`; without it
the link fails with undefined references to `ur_bundled_sha256_*`
(deliberate — a link error instead of a silent runtime hardfault).

### MicroPython wrapper (`uUR.c`)

`uUR.c` at the repo root wraps the C library as a MicroPython module
exposing `URDecoder`, `UREncoder`, `UR`, and `FountainEncoder`. On
Make-based MicroPython ports, `micropython.mk` builds it with the
bundled SHA-256 (point `USER_C_MODULES` at the directory containing this
repo). Legacy ports wire it in their own build instead.

### CPython module (`python/uUR.c`)

`pip install .` builds the same `uUR` module for CPython, with the
bundled SHA-256.

## Repository layout

```
src/
  ur_encoder.c, ur_decoder.c, ur.c   # top-level UR API
  fountain_encoder.c, fountain_decoder.c, fountain_utils.c
  bytewords.c, crc32.c, utils.c
  sha256/                            # bundled SHA-256 + backend selector
  types/                             # CBOR + per-type encode/decode
tests/
  test_ur_*.c                        # test suites, one binary each
  test_harness.{c,h}, test_utils.{c,h}
  test_cases/                        # fixtures organized by type
  esp-idf-smoke/                     # ESP-IDF build check (CI)
python/uUR.c                         # CPython binding
scripts/coverage.sh                  # wrapped by `make coverage`
uUR.c                                # MicroPython binding
CMakeLists.txt, Kconfig              # ESP-IDF component
micropython.mk                       # MicroPython USERMOD
setup.py, pyproject.toml             # CPython package
format-src.sh                        # clang-format the C sources
```

## License

BSD-2-Clause Plus Patent License. Copyright © 2025 cUR Contributors.
See [LICENSE](LICENSE) for full text.

Independent implementation written using foundation-ur-py as a
reference for validation; does not derive from that codebase.
