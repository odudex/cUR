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
#include <stdbool.h>

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

void crc32_init(void) {
  if (crc32_table_initialized)
    return;

  const uint32_t polynomial = 0xEDB88320;

  for (uint32_t i = 0; i < 256; i++) {
    uint32_t crc = i;
    for (int j = 8; j > 0; j--) {
      if (crc & 1) {
        crc = (crc >> 1) ^ polynomial;
      } else {
        crc >>= 1;
      }
    }
    crc32_table[i] = crc;
  }
  crc32_table_initialized = true;
}

uint32_t crc32_calculate(const uint8_t *data, size_t length) {
  if (!data || length == 0)
    return 0;

  crc32_init();

  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
  }
  return ~crc & 0xFFFFFFFF;
}