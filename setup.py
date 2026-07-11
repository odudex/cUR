"""CPython build for the native `uUR` BC-UR module.

Compiles the portable cUR core (envelope + types + bundled SHA-256) together
with the CPython binding in `python/uUR.c` into a single `uUR` extension. This
is the desktop/host counterpart of the MicroPython `micropython.mk` wiring, so
the same module (`import uUR`) is available on CPython (e.g. Raspberry Pi).

Host build uses the bundled `src/sha256/sha256.c` (UR_USE_MBEDTLS_SHA256 is NOT
defined), so there is no OpenSSL/mbedTLS dependency.
"""

from __future__ import annotations

import glob

from setuptools import Extension, setup

# Source paths MUST be relative to this setup.py directory — setuptools' PEP 517
# wheel build rejects absolute paths. glob runs with cwd == the project root
# (the source tree pip builds in), so relative globs resolve correctly.
#
# Portable C core: envelope (ur/fountain/bytewords/crc32/utils) + typed CBOR
# payload codecs (src/types) + the bundled SHA-256. Mirrors UR_SRCS in
# CMakeLists.txt (non-envelope-only) plus src/sha256/sha256.c for the host path.
core_sources = sorted(
    glob.glob("src/*.c")
    + glob.glob("src/types/*.c")
    + glob.glob("src/sha256/sha256.c")
)

uur_ext = Extension(
    name="uUR",
    sources=["python/uUR.c", *core_sources],
    # cUR root ('.') satisfies the binding's `src/...` includes; 'src' satisfies
    # the core's sibling includes (e.g. "sha256/sha256.h", "ur_decoder.h").
    include_dirs=[".", "src"],
    # Bundled SHA-256 (host): do NOT define UR_USE_MBEDTLS_SHA256.
)

setup(ext_modules=[uur_ext])
