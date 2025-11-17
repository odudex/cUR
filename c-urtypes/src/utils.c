#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>

// Memory management utilities
void *urtypes_safe_malloc(size_t size) {
    if (size == 0) return NULL;
    void *ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void *urtypes_safe_realloc(void *ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    void *new_ptr = realloc(ptr, size);
    return new_ptr;
}

char *urtypes_safe_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *dup = urtypes_safe_malloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

void urtypes_safe_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}

// String utilities
char *urtypes_str_concat(const char *s1, const char *s2) {
    if (!s1 && !s2) return NULL;
    if (!s1) return urtypes_safe_strdup(s2);
    if (!s2) return urtypes_safe_strdup(s1);

    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    char *result = urtypes_safe_malloc(len1 + len2 + 1);
    if (result) {
        memcpy(result, s1, len1);
        memcpy(result + len1, s2, len2 + 1);
    }
    return result;
}

char *urtypes_str_concat_n(int count, ...) {
    if (count <= 0) return NULL;

    va_list args;
    size_t total_len = 0;

    // Calculate total length
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        const char *str = va_arg(args, const char *);
        if (str) {
            total_len += strlen(str);
        }
    }
    va_end(args);

    // Allocate and concatenate
    char *result = urtypes_safe_malloc(total_len + 1);
    if (!result) return NULL;

    char *ptr = result;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        const char *str = va_arg(args, const char *);
        if (str) {
            size_t len = strlen(str);
            memcpy(ptr, str, len);
            ptr += len;
        }
    }
    va_end(args);
    *ptr = '\0';

    return result;
}

char *urtypes_bytes_to_hex(const uint8_t *data, size_t len) {
    if (!data || len == 0) return NULL;

    char *hex = urtypes_safe_malloc(len * 2 + 1);
    if (!hex) return NULL;

    for (size_t i = 0; i < len; i++) {
        sprintf(hex + i * 2, "%02x", data[i]);
    }
    hex[len * 2] = '\0';

    return hex;
}

uint8_t *urtypes_hex_to_bytes(const char *hex, size_t *out_len) {
    if (!hex || !out_len) return NULL;

    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return NULL;

    size_t byte_len = hex_len / 2;
    uint8_t *bytes = urtypes_safe_malloc(byte_len);
    if (!bytes) return NULL;

    for (size_t i = 0; i < byte_len; i++) {
        char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        bytes[i] = (uint8_t)strtol(byte_str, NULL, 16);
    }

    *out_len = byte_len;
    return bytes;
}

// Array utilities
urtypes_array_t *urtypes_array_new(void) {
    urtypes_array_t *arr = urtypes_safe_malloc(sizeof(urtypes_array_t));
    if (!arr) return NULL;

    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;

    return arr;
}

void urtypes_array_free(urtypes_array_t *arr) {
    if (!arr) return;

    urtypes_safe_free(arr->items);
    urtypes_safe_free(arr);
}

bool urtypes_array_append(urtypes_array_t *arr, void *item) {
    if (!arr) return false;

    if (arr->count >= arr->capacity) {
        size_t new_capacity = arr->capacity == 0 ? 4 : arr->capacity * 2;
        void **new_items = urtypes_safe_realloc(arr->items, new_capacity * sizeof(void *));
        if (!new_items) return false;

        arr->items = new_items;
        arr->capacity = new_capacity;
    }

    arr->items[arr->count++] = item;
    return true;
}

void *urtypes_array_get(urtypes_array_t *arr, size_t index) {
    if (!arr || index >= arr->count) return NULL;
    return arr->items[index];
}

size_t urtypes_array_count(urtypes_array_t *arr) {
    if (!arr) return 0;
    return arr->count;
}

// Byte buffer utilities
byte_buffer_t *byte_buffer_new(void) {
    return byte_buffer_new_with_capacity(16);
}

byte_buffer_t *byte_buffer_new_with_capacity(size_t capacity) {
    byte_buffer_t *buf = urtypes_safe_malloc(sizeof(byte_buffer_t));
    if (!buf) return NULL;

    buf->data = urtypes_safe_malloc(capacity);
    if (!buf->data) {
        urtypes_safe_free(buf);
        return NULL;
    }

    buf->len = 0;
    buf->capacity = capacity;

    return buf;
}

byte_buffer_t *byte_buffer_new_from_data(const uint8_t *data, size_t len) {
    if (!data || len == 0) return NULL;

    byte_buffer_t *buf = byte_buffer_new_with_capacity(len);
    if (!buf) return NULL;

    memcpy(buf->data, data, len);
    buf->len = len;

    return buf;
}

void byte_buffer_free(byte_buffer_t *buf) {
    if (!buf) return;

    urtypes_safe_free(buf->data);
    urtypes_safe_free(buf);
}

bool byte_buffer_append(byte_buffer_t *buf, const uint8_t *data, size_t len) {
    if (!buf) return false;
    // Allow zero-length append (no-op) and NULL data if len is 0
    if (len == 0) return true;
    if (!data) return false;

    if (buf->len + len > buf->capacity) {
        size_t new_capacity = buf->capacity;
        while (new_capacity < buf->len + len) {
            new_capacity *= 2;
        }

        uint8_t *new_data = urtypes_safe_realloc(buf->data, new_capacity);
        if (!new_data) return false;

        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->len, data, len);
    buf->len += len;

    return true;
}

bool byte_buffer_append_byte(byte_buffer_t *buf, uint8_t byte) {
    return byte_buffer_append(buf, &byte, 1);
}

uint8_t *byte_buffer_get_data(byte_buffer_t *buf) {
    if (!buf) return NULL;
    return buf->data;
}

size_t byte_buffer_get_len(byte_buffer_t *buf) {
    if (!buf) return 0;
    return buf->len;
}
