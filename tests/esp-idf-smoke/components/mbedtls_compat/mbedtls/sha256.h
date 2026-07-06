// Compatibility shim (vendored from Kern): mbedTLS v4 moved sha256.h to
// mbedtls/private/. MBEDTLS_ALLOW_PRIVATE_ACCESS keeps the context struct
// fields reachable through the private header.
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include "mbedtls/private/sha256.h"
