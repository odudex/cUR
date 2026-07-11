//
// utils.c
//
// Copyright © 2025 Krux Contributors
// Licensed under the "BSD-2-Clause Plus Patent License"
//
// General utility functions.
//
// This is an independent implementation written using
// foundation-ur-py as a reference for testing and validation.
//

#include "utils.h"
#include "xor_internal.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(UR_XOR_ESP32P4_SIMD)
#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "UR_XOR_ESP32P4_SIMD requires ESP32-P4"
#endif

// Pointers are pinned because esp.vld/esp.vst require low address registers.
static inline void ur_xor128_inplace(uint8_t *dst, const uint8_t *src,
                                     size_t chunks) {
  register uint8_t *d __asm__("a0") = dst;
  register const uint8_t *s __asm__("a1") = src;
  __asm__ volatile("1:\n"
                   "esp.vld.128.ip q0, a1, 16\n"
                   "esp.vld.128.ip q1, a0, 0\n"
                   "esp.xorq q0, q0, q1\n"
                   "esp.vst.128.ip q0, a0, 16\n"
                   "addi %[c], %[c], -1\n"
                   "bnez %[c], 1b\n"
                   : [c] "+r"(chunks), "+r"(d), "+r"(s)
                   :
                   : "memory");
}
static inline void ur_xor128_out(uint8_t *out, const uint8_t *a,
                                 const uint8_t *b, size_t chunks) {
  register uint8_t *o __asm__("a0") = out;
  register const uint8_t *pa __asm__("a1") = a;
  register const uint8_t *pb __asm__("a2") = b;
  __asm__ volatile("1:\n"
                   "esp.vld.128.ip q0, a1, 16\n"
                   "esp.vld.128.ip q1, a2, 16\n"
                   "esp.xorq q0, q0, q1\n"
                   "esp.vst.128.ip q0, a0, 16\n"
                   "addi %[c], %[c], -1\n"
                   "bnez %[c], 1b\n"
                   : [c] "+r"(chunks), "+r"(o), "+r"(pa), "+r"(pb)
                   :
                   : "memory");
}
#endif

// In-place XOR: dst[i] ^= src[i] for n bytes. Word-wise (8 bytes/iter) with a
// scalar tail -- roughly 4x faster than a byte loop. On ESP32-P4 with
// UR_XOR_ESP32P4_SIMD, 16-byte-aligned runs use the PIE 128-bit path (~16x).
// Buffers must not overlap.
void ur_xor_inplace(uint8_t *restrict dst, const uint8_t *restrict src,
                    size_t n) {
#if defined(UR_XOR_ESP32P4_SIMD)
  size_t head = (size_t)((0u - (uintptr_t)dst) & 15u);
  if (head > n)
    head = n;
  for (size_t h = 0; h < head; h++)
    dst[h] ^= src[h];
  dst += head;
  src += head;
  n -= head;
  if (((uintptr_t)src & 15u) == 0 && n >= 16) {
    size_t chunks = n >> 4;
    ur_xor128_inplace(dst, src, chunks);
    size_t done = chunks << 4;
    dst += done;
    src += done;
    n -= done;
  }
#endif
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t a, b;
    memcpy(&a, dst + i, 8);
    memcpy(&b, src + i, 8);
    a ^= b;
    memcpy(dst + i, &a, 8);
  }
  for (; i < n; i++)
    dst[i] ^= src[i];
}

// Out-of-place XOR: out[i] = a[i] ^ b[i] for n bytes. Buffers must not overlap.
void ur_xor(uint8_t *restrict out, const uint8_t *restrict a,
            const uint8_t *restrict b, size_t n) {
#if defined(UR_XOR_ESP32P4_SIMD)
  size_t head = (size_t)((0u - (uintptr_t)out) & 15u);
  if (head > n)
    head = n;
  for (size_t h = 0; h < head; h++)
    out[h] = a[h] ^ b[h];
  out += head;
  a += head;
  b += head;
  n -= head;
  if (((uintptr_t)a & 15u) == 0 && ((uintptr_t)b & 15u) == 0 && n >= 16) {
    size_t chunks = n >> 4;
    ur_xor128_out(out, a, b, chunks);
    size_t done = chunks << 4;
    out += done;
    a += done;
    b += done;
    n -= done;
  }
#endif
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t x, y;
    memcpy(&x, a + i, 8);
    memcpy(&y, b + i, 8);
    x ^= y;
    memcpy(out + i, &x, 8);
  }
  for (; i < n; i++)
    out[i] = a[i] ^ b[i];
}

/* Allocator routing. On an ESP32 with external SPIRAM, the fountain decoder's
 * working set — up to MAX_MIXED_PARTS part buffers plus per-part fragments,
 * allocated and freed as parts arrive and are XOR-reduced — is many small,
 * variably-sized allocations. ESP-IDF's default malloc() forces allocations at
 * or below CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (16 KB by default) into scarce
 * INTERNAL RAM, where they steadily grow the footprint and, worse, fragment the
 * heap: the largest contiguous free block shrinks well below total free. The
 * damaging symptom is not the decode itself running out of memory, but a later
 * allocation that needs a contiguous internal block failing even with ample
 * free RAM — e.g. re-initializing another internal-RAM subsystem after a scan
 * (re-launching the camera, whose driver needs a contiguous internal buffer/
 * stack the fragmented heap can no longer supply). These are plain CPU-accessed
 * byte buffers, so route them to PSRAM instead (falling back to internal RAM if
 * no PSRAM is present). Off-device this is a no-op. */
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *ur_heap_malloc(size_t size) {
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void *ur_heap_realloc(void *ptr, size_t size) {
  void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p
           : heap_caps_realloc(ptr, size,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
#define UR_MALLOC(sz) ur_heap_malloc(sz)
#define UR_REALLOC(p, sz) ur_heap_realloc((p), (sz))
#else
#define UR_MALLOC(sz) malloc(sz)
#define UR_REALLOC(p, sz) realloc((p), (sz))
#endif

bool str_has_prefix(const char *str, const char *prefix) {
  if (!str || !prefix)
    return false;
  return strncmp(str, prefix, strlen(prefix)) == 0;
}

void str_to_lower(char *str) {
  if (!str)
    return;
  for (char *p = str; *p; p++) {
    *p = tolower((unsigned char)*p);
  }
}

size_t str_split(const char *str, char delimiter, char **parts,
                 size_t max_parts) {
  if (!str || !parts || max_parts == 0)
    return 0;

  size_t count = 0;
  const char *start = str;
  const char *end = str;

  while (*end && count < max_parts) {
    if (*end == delimiter || *(end + 1) == '\0') {
      size_t len = end - start;
      if (*end != delimiter && *(end + 1) == '\0') {
        len++;
      }

      if (len > 0) {
        parts[count] = safe_malloc(len + 1);
        if (parts[count]) {
          strncpy(parts[count], start, len);
          parts[count][len] = '\0';
          count++;
        }
      }
      start = end + 1;
    }
    end++;
  }

  return count;
}

bool is_ur_type(const char *type) {
  if (!type || *type == '\0' || *type == '-')
    return false;

  char last = 0;
  for (const char *p = type; *p; p++) {
    if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) &&
        *p != '-') {
      return false;
    }
    last = *p;
  }

  return last != '-';
}

bool parse_ur_string(const char *ur_str, char **type, char ***components,
                     size_t *component_count) {
  if (!ur_str || !type || !components || !component_count)
    return false;

  size_t len = strlen(ur_str);
  char *lowered = safe_malloc(len + 1);
  if (!lowered)
    return false;

  memcpy(lowered, ur_str, len + 1);
  str_to_lower(lowered);

  if (len < 3 || lowered[0] != 'u' || lowered[1] != 'r' || lowered[2] != ':') {
    free(lowered);
    return false;
  }

  char *path = lowered + 3;
  if (*path == '\0') {
    free(lowered);
    return false;
  }

  // Split in-place by replacing '/' with '\0' and collecting pointers
  // Max 10 parts (same as before)
  char *part_ptrs[10];
  size_t part_count = 0;
  char *start = path;

  for (char *p = path;; p++) {
    if (*p == '/' || *p == '\0') {
      bool is_end = (*p == '\0');
      if (p > start && part_count < 10) {
        *p = '\0';
        part_ptrs[part_count++] = start;
      }
      if (is_end)
        break;
      start = p + 1;
    }
  }

  if (part_count < 2) {
    free(lowered);
    return false;
  }

  if (!is_ur_type(part_ptrs[0])) {
    free(lowered);
    return false;
  }

  *type = safe_strdup(part_ptrs[0]);
  *component_count = part_count - 1;
  *components = safe_malloc(sizeof(char *) * (*component_count));

  if (!*type || !*components) {
    if (*type)
      free(*type);
    if (*components)
      free(*components);
    free(lowered);
    return false;
  }

  for (size_t i = 0; i < *component_count; i++) {
    (*components)[i] = safe_strdup(part_ptrs[i + 1]);
    if (!(*components)[i]) {
      free(*type);
      free_string_array(*components, i);
      free(*components);
      free(lowered);
      return false;
    }
  }

  free(lowered);
  return true;
}

bool parse_sequence_component(const char *seq_str, uint32_t *seq_num,
                              size_t *seq_len) {
  if (!seq_str || !seq_num || !seq_len)
    return false;

  char **parts = safe_malloc(sizeof(char *) * 2);
  if (!parts)
    return false;

  size_t part_count = str_split(seq_str, '-', parts, 2);
  if (part_count != 2) {
    free_string_array(parts, part_count);
    free(parts);
    return false;
  }

  char *endptr;
  unsigned long num = strtoul(parts[0], &endptr, 10);
  if (*endptr != '\0' || num == 0) {
    free_string_array(parts, part_count);
    free(parts);
    return false;
  }
  *seq_num = (uint32_t)num;

  unsigned long len = strtoul(parts[1], &endptr, 10);
  if (*endptr != '\0' || len == 0) {
    free_string_array(parts, part_count);
    free(parts);
    return false;
  }
  *seq_len = (size_t)len;

  free_string_array(parts, part_count);
  free(parts);
  return true;
}

void free_string_array(char **strings, size_t count) {
  if (!strings)
    return;
  for (size_t i = 0; i < count; i++) {
    if (strings[i]) {
      free(strings[i]);
    }
  }
}

void *safe_malloc(size_t size) {
  if (size == 0)
    return NULL;
  void *ptr = UR_MALLOC(size);
  if (ptr) {
    memset(ptr, 0, size);
  }
  return ptr;
}

void *safe_malloc_uninit(size_t size) {
  if (size == 0)
    return NULL;
  return UR_MALLOC(size);
}

void *safe_realloc(void *ptr, size_t size) { return UR_REALLOC(ptr, size); }

char *safe_strdup(const char *str) {
  if (!str)
    return NULL;
  size_t len = strlen(str);
  char *dup = safe_malloc(len + 1);
  if (dup) {
    strcpy(dup, str);
  }
  return dup;
}
