# cUR — C implementation of Blockchain Commons Uniform Resources

A lightweight C implementation of the [Blockchain Commons Uniform Resources (UR)](https://github.com/BlockchainCommons/Research/blob/master/papers/bcr-2020-005-ur.md)
specification — a method for encoding structured binary data optimized
for transport via QR codes.

Targets PC and ESP-IDF (e.g. ESP32, ESP32-P4) from a single source
tree. SHA-256 backend is selected at compile time to use the
platform's hardware when available.

The implementation favors being small and easy to review. Optional
performance paths exist, but every default is the simplest, smallest
variant; see [Build options](#build-options).

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
state after each part (`ur_decoder_get_state()` polls it without feeding).
`UR_DECODER_PROCESSING` means keep feeding parts; `UR_DECODER_OK` means
done with a result available; terminal states (`UR_DECODER_OK`,
`UR_DECODER_NO_RESULT`, `UR_DECODER_ERROR_INVALID_CHECKSUM`) are
permanent, while every other error is transient — the offending frame was
rejected, and it is safe to keep feeding parts (misread QR frames are
expected during scanning).

> **Migrating from the `is_complete`/`is_success`/`get_last_error` API:**
> `ur_decoder_receive_part()` now returns `ur_decoder_state_t`, and
> `UR_DECODER_OK == 0`, so a legacy `if (ur_decoder_receive_part(...))`
> check silently inverts its meaning. Compare the return value against
> the state constants (or use `ur_decoder_state_is_error()` /
> `ur_decoder_state_is_terminal()`); never treat it as a boolean.

> **Progress API type change:** `ur_decoder_estimated_percent_complete()`
> and `ur_decoder_estimated_percent_complete_weighted()` (and the
> `fountain_decoder_*` equivalents) now return single-precision `float`
> instead of `double` — they are display-only estimates, and embedded
> FPUs are single-precision. Fragment-selection interop math is unchanged
> and deliberately remains `double`.

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
| `UR_CRC32_SLICE_BY_8` | off | Slice-by-8 CRC32: ~2.7x faster, +8 KB flash for a const table. The default 64-byte nibble table is rarely a bottleneck (CRCs run per fragment, a few hundred bytes each). |
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
exposing `URDecoder`, `UREncoder`, `UR`, and `FountainEncoder`. For
vanilla MicroPython USERMOD builds the repo-root `micropython.mk`
provides the wiring; legacy ports wire it in their own build instead.

## Repository layout

```
src/
  ur_encoder.c, ur_decoder.c, ur.c   # top-level UR API
  fountain_encoder.c, fountain_decoder.c, fountain_utils.c
  bytewords.c, crc32.c
  sha256/                            # bundled SHA-256 + backend selector
  types/                             # CBOR + per-type encode/decode
tests/
  test_ur_*.c                        # one per UR type + negative
  test_harness.{c,h}, test_utils.{c,h}
  test_cases/                        # fixtures organized by type
scripts/coverage.sh                  # wrapped by `make coverage`
uUR.c                                # MicroPython wrapper
CMakeLists.txt                       # ESP-IDF component
micropython.mk                       # vanilla MicroPython USERMOD
```

## License

BSD-2-Clause Plus Patent License. Copyright © 2025 Krux Contributors.
See [LICENSE](LICENSE) for full text.

Independent implementation written using foundation-ur-py as a
reference for validation; does not derive from that codebase.
