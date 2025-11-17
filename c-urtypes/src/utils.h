#ifndef URTYPES_UTILS_H
#define URTYPES_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Memory management utilities (prefixed to avoid conflicts with bc-ur)
void *urtypes_safe_malloc(size_t size);
void *urtypes_safe_realloc(void *ptr, size_t size);
char *urtypes_safe_strdup(const char *str);
void urtypes_safe_free(void *ptr);

// String utilities
char *urtypes_str_concat(const char *s1, const char *s2);
char *urtypes_str_concat_n(int count, ...);
char *urtypes_bytes_to_hex(const uint8_t *data, size_t len);
uint8_t *urtypes_hex_to_bytes(const char *hex, size_t *out_len);

// Wrapper macros for convenience (so the rest of the code doesn't need changes)
#define safe_malloc urtypes_safe_malloc
#define safe_realloc urtypes_safe_realloc
#define safe_strdup urtypes_safe_strdup
#define safe_free urtypes_safe_free
#define str_concat urtypes_str_concat
#define str_concat_n urtypes_str_concat_n
#define bytes_to_hex urtypes_bytes_to_hex
#define hex_to_bytes urtypes_hex_to_bytes

// Array utilities (prefixed to avoid conflicts)
typedef struct {
    void **items;
    size_t count;
    size_t capacity;
} urtypes_array_t;

urtypes_array_t *urtypes_array_new(void);
void urtypes_array_free(urtypes_array_t *arr);
bool urtypes_array_append(urtypes_array_t *arr, void *item);
void *urtypes_array_get(urtypes_array_t *arr, size_t index);
size_t urtypes_array_count(urtypes_array_t *arr);

// Wrapper macros
#define array_t urtypes_array_t
#define array_new urtypes_array_new
#define array_free urtypes_array_free
#define array_append urtypes_array_append
#define array_get urtypes_array_get
#define array_count urtypes_array_count

// Byte buffer utilities
typedef struct {
    uint8_t *data;
    size_t len;
    size_t capacity;
} byte_buffer_t;

byte_buffer_t *byte_buffer_new(void);
byte_buffer_t *byte_buffer_new_with_capacity(size_t capacity);
byte_buffer_t *byte_buffer_new_from_data(const uint8_t *data, size_t len);
void byte_buffer_free(byte_buffer_t *buf);
bool byte_buffer_append(byte_buffer_t *buf, const uint8_t *data, size_t len);
bool byte_buffer_append_byte(byte_buffer_t *buf, uint8_t byte);
uint8_t *byte_buffer_get_data(byte_buffer_t *buf);
size_t byte_buffer_get_len(byte_buffer_t *buf);

#endif // URTYPES_UTILS_H
