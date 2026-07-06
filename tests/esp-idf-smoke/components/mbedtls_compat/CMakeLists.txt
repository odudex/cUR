# Minimal vendored copy of the Kern project's mbedtls_compat shim, trimmed
# to SHA-256 (the only mbedTLS API cUR uses). mbedTLS v4 (ESP-IDF >= 6.0)
# moved the crypto headers from mbedtls/ to mbedtls/private/; this
# header-only component redirects #include <mbedtls/sha256.h> so
# src/sha256/sha256_compat.h keeps working. Real consumers on IDF >= 6
# (e.g. Kern) provide their own copy — see the repo-root CMakeLists.txt.
idf_component_register(
    INCLUDE_DIRS "."
    REQUIRES mbedtls
)
