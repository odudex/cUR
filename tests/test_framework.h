#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// Test statistics
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Color codes for terminal output
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_RESET   "\x1b[0m"

// Test result macros
#define TEST_PASS() do { \
    tests_passed++; \
    printf(COLOR_GREEN "  ✓ PASS" COLOR_RESET "\n"); \
} while(0)

#define TEST_FAIL(msg) do { \
    tests_failed++; \
    printf(COLOR_RED "  ✗ FAIL: %s" COLOR_RESET "\n", msg); \
} while(0)

// Assertion macros
#define ASSERT_TRUE(condition, msg) do { \
    tests_run++; \
    if (condition) { \
        TEST_PASS(); \
    } else { \
        TEST_FAIL(msg); \
    } \
} while(0)

#define ASSERT_FALSE(condition, msg) do { \
    tests_run++; \
    if (!(condition)) { \
        TEST_PASS(); \
    } else { \
        TEST_FAIL(msg); \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr, msg) do { \
    tests_run++; \
    if ((ptr) != NULL) { \
        TEST_PASS(); \
    } else { \
        TEST_FAIL(msg); \
    } \
} while(0)

#define ASSERT_NULL(ptr, msg) do { \
    tests_run++; \
    if ((ptr) == NULL) { \
        TEST_PASS(); \
    } else { \
        TEST_FAIL(msg); \
    } \
} while(0)

#define ASSERT_EQUAL(a, b, msg) do { \
    tests_run++; \
    if ((a) == (b)) { \
        TEST_PASS(); \
    } else { \
        TEST_FAIL(msg); \
    } \
} while(0)

#define ASSERT_BYTES_EQUAL(a, b, len, msg) do { \
    tests_run++; \
    if (memcmp((a), (b), (len)) == 0) { \
        TEST_PASS(); \
    } else { \
        char fail_msg[256]; \
        snprintf(fail_msg, sizeof(fail_msg), "%s (bytes differ)", msg); \
        TEST_FAIL(fail_msg); \
    } \
} while(0)

#define ASSERT_STRING_EQUAL(a, b, msg) do { \
    tests_run++; \
    if (strcmp((a), (b)) == 0) { \
        TEST_PASS(); \
    } else { \
        char fail_msg[256]; \
        snprintf(fail_msg, sizeof(fail_msg), "%s (expected '%s', got '%s')", msg, b, a); \
        TEST_FAIL(fail_msg); \
    } \
} while(0)

// Test suite macros
#define TEST_SUITE_START(name) do { \
    printf("\n" COLOR_YELLOW "Running test suite: %s" COLOR_RESET "\n", name); \
    tests_run = 0; \
    tests_passed = 0; \
    tests_failed = 0; \
} while(0)

#define TEST_SUITE_END() do { \
    printf("\n" COLOR_YELLOW "Test Results:" COLOR_RESET "\n"); \
    printf("  Total:  %d\n", tests_run); \
    printf(COLOR_GREEN "  Passed: %d" COLOR_RESET "\n", tests_passed); \
    if (tests_failed > 0) { \
        printf(COLOR_RED "  Failed: %d" COLOR_RESET "\n", tests_failed); \
    } else { \
        printf("  Failed: 0\n"); \
    } \
    printf("\n"); \
} while(0)

#define TEST_CASE(name) do { \
    printf("\n" "Test: %s\n", name); \
} while(0)

// Helper function to print bytes in hex
static void print_hex(const char *label, const uint8_t *data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

// Helper function to convert hex string to bytes
static uint8_t *hex_to_bytes(const char *hex, size_t *out_len) {
    size_t len = strlen(hex);
    if (len % 2 != 0) {
        return NULL;
    }

    size_t byte_len = len / 2;
    uint8_t *bytes = malloc(byte_len);
    if (!bytes) {
        return NULL;
    }

    for (size_t i = 0; i < byte_len; i++) {
        sscanf(hex + 2*i, "%2hhx", &bytes[i]);
    }

    *out_len = byte_len;
    return bytes;
}

// Helper function to convert bytes to hex string
static char *bytes_to_hex(const uint8_t *data, size_t len) {
    char *hex = malloc(len * 2 + 1);
    if (!hex) {
        return NULL;
    }

    for (size_t i = 0; i < len; i++) {
        sprintf(hex + 2*i, "%02x", data[i]);
    }
    hex[len * 2] = '\0';

    return hex;
}

#endif // TEST_FRAMEWORK_H
