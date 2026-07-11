//
// crc32.c
//
// Copyright © 2025 Krux Contributors
// Licensed under the "BSD-2-Clause Plus Patent License"
//
// CRC32 checksum implementation for data integrity verification.
//
// This is an independent implementation written using
// foundation-ur-py as a reference for testing and validation.
//

#include "crc32.h"

#if defined(UR_CRC32_SLICE_BY_8)

#include <string.h>

// Fail closed: the slice-by-8 kernel does little-endian word loads, so refuse
// to build unless the byte order is known to be little-endian. A compiler that
// doesn't define __BYTE_ORDER__ can't prove it, so it must use the portable
// nibble table (build without UR_CRC32_SLICE_BY_8) instead of silently
// computing wrong CRCs.
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "UR_CRC32_SLICE_BY_8 requires a known little-endian byte order"
#endif

#include "crc32_slice_table.h"

uint32_t crc32_calculate(const uint8_t *data, size_t length) {
  if (!data || length == 0)
    return 0;

  uint32_t crc = 0xFFFFFFFFu;
  size_t i = 0;
  for (; i + 8 <= length; i += 8) {
    uint32_t lo, hi;
    memcpy(&lo, data + i, 4);
    memcpy(&hi, data + i + 4, 4);
    lo ^= crc;
    crc = crc32_slice[7][lo & 0xFF] ^ crc32_slice[6][(lo >> 8) & 0xFF] ^
          crc32_slice[5][(lo >> 16) & 0xFF] ^
          crc32_slice[4][(lo >> 24) & 0xFF] ^ crc32_slice[3][hi & 0xFF] ^
          crc32_slice[2][(hi >> 8) & 0xFF] ^ crc32_slice[1][(hi >> 16) & 0xFF] ^
          crc32_slice[0][(hi >> 24) & 0xFF];
  }
  for (; i < length; i++)
    crc = crc32_slice[0][(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

#else // fallback: nibble-based (4-bit) table — 64 bytes ROM, 2 lookups/byte

// Nibble-based CRC32 table (polynomial 0xEDB88320) - 64 bytes ROM
static const uint32_t crc32_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
    0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};

uint32_t crc32_calculate(const uint8_t *data, size_t length) {
  if (!data || length == 0)
    return 0;

  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc = (crc >> 4) ^ crc32_table[(crc ^ data[i]) & 0x0F];
    crc = (crc >> 4) ^ crc32_table[(crc ^ (data[i] >> 4)) & 0x0F];
  }
  return ~crc;
}

#endif
