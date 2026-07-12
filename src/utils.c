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

// Word type for the portable XOR loops: pointer-width chunks, so 32-bit
// targets don't emulate 64-bit arithmetic with register pairs.
#if UINTPTR_MAX > 0xFFFFFFFFu
typedef uint64_t ur_xor_word_t;
#else
typedef uint32_t ur_xor_word_t;
#endif

// Portable word-wise XOR loops (byte tail). memcpy is the aliasing- and
// alignment-safe word access and compiles to plain loads/stores wherever the
// target allows them; pointer casts here would be undefined behavior under
// strict aliasing and fault on strict-alignment CPUs.
static void ur_xor_portable_inplace(uint8_t *restrict dst,
                                    const uint8_t *restrict src, size_t n) {
  size_t i = 0;
  for (; i + sizeof(ur_xor_word_t) <= n; i += sizeof(ur_xor_word_t)) {
    ur_xor_word_t a, b;
    memcpy(&a, dst + i, sizeof(a));
    memcpy(&b, src + i, sizeof(b));
    a ^= b;
    memcpy(dst + i, &a, sizeof(a));
  }
  for (; i < n; i++)
    dst[i] ^= src[i];
}

static void ur_xor_portable(uint8_t *restrict out, const uint8_t *restrict a,
                            const uint8_t *restrict b, size_t n) {
  size_t i = 0;
  for (; i + sizeof(ur_xor_word_t) <= n; i += sizeof(ur_xor_word_t)) {
    ur_xor_word_t x, y;
    memcpy(&x, a + i, sizeof(x));
    memcpy(&y, b + i, sizeof(y));
    x ^= y;
    memcpy(out + i, &x, sizeof(x));
  }
  for (; i < n; i++)
    out[i] = a[i] ^ b[i];
}

#if defined(UR_XOR_ESP32P4_SIMD)
#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "UR_XOR_ESP32P4_SIMD requires ESP32-P4"
#endif

// ESP32-P4 PIE (xesppie) implementations: scalar head bytes until the
// destination is 16-byte aligned, 128-bit vector body when the operands are
// co-aligned, portable word-wise loop for the tail (and as the fallback when
// co-alignment is impossible).

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

// In-place XOR: dst[i] ^= src[i] for n bytes. Buffers must not overlap.
void ur_xor_inplace(uint8_t *restrict dst, const uint8_t *restrict src,
                    size_t n) {
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
  ur_xor_portable_inplace(dst, src, n);
}

// Out-of-place XOR: out[i] = a[i] ^ b[i] for n bytes. Buffers must not overlap.
void ur_xor(uint8_t *restrict out, const uint8_t *restrict a,
            const uint8_t *restrict b, size_t n) {
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
  ur_xor_portable(out, a, b, n);
}

#else // portable builds: the word-wise loops are the whole implementation

// In-place XOR: dst[i] ^= src[i] for n bytes. Buffers must not overlap.
void ur_xor_inplace(uint8_t *restrict dst, const uint8_t *restrict src,
                    size_t n) {
  ur_xor_portable_inplace(dst, src, n);
}

// Out-of-place XOR: out[i] = a[i] ^ b[i] for n bytes. Buffers must not overlap.
void ur_xor(uint8_t *restrict out, const uint8_t *restrict a,
            const uint8_t *restrict b, size_t n) {
  ur_xor_portable(out, a, b, n);
}

#endif

/* On ESP32 targets, route allocations to PSRAM (falling back to internal RAM
 * when absent). ESP-IDF's malloc() keeps small allocations in internal RAM,
 * where the fountain decoder's churn of variably-sized part buffers fragments
 * that scarce heap. These are plain CPU-accessed byte buffers, so cached PSRAM
 * is fine, and free() works on heap_caps allocations. Off-device: no-op.
 * Opt out with UR_NO_PSRAM_ALLOC (Kconfig: UR_ALLOC_PSRAM).
 *
 * With UR_XOR_ESP32P4_SIMD, allocations are additionally 16-byte aligned (the
 * plain heap guarantees only 8): the PIE vector path requires 16-byte
 * co-aligned XOR operands, and every fountain buffer comes from these
 * allocators, so aligning here — the single choke point — makes the SIMD path
 * engage instead of falling back to the word loop. Shrink-reallocs (received
 * fragments) resize in place and keep the aligned address. This applies even
 * when PSRAM routing is opted out. */
#if defined(ESP_PLATFORM) &&                                                   \
    (!defined(UR_NO_PSRAM_ALLOC) || defined(UR_XOR_ESP32P4_SIMD))
#include "esp_heap_caps.h"

#if defined(UR_XOR_ESP32P4_SIMD)
#define ur_caps_alloc(sz, caps) heap_caps_aligned_alloc(16, (sz), (caps))
#else
#define ur_caps_alloc(sz, caps) heap_caps_malloc((sz), (caps))
#endif

static void *ur_heap_malloc(size_t size) {
#if !defined(UR_NO_PSRAM_ALLOC)
  void *p = ur_caps_alloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p)
    return p;
  return ur_caps_alloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
  /* PSRAM routing opted out: same heaps malloc() would pick, just aligned. */
  return ur_caps_alloc(size, MALLOC_CAP_DEFAULT);
#endif
}
#define UR_MALLOC(sz) ur_heap_malloc(sz)

#if !defined(UR_NO_PSRAM_ALLOC)
static void *ur_heap_realloc(void *ptr, size_t size) {
  if (size == 0) {
    /* heap_caps_realloc(ptr, 0, caps) frees ptr and returns NULL; retrying
     * with other caps would double-free. Match glibc: single free. */
    free(ptr);
    return NULL;
  }
  void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p
           : heap_caps_realloc(ptr, size,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
#define UR_REALLOC(p, sz) ur_heap_realloc((p), (sz))
#else
#define UR_REALLOC(p, sz) realloc((p), (sz))
#endif

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
