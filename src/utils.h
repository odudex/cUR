#ifndef UR_UTILS_H
#define UR_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// String manipulation utilities

/**
 * Check if string has prefix
 * @param str String to check
 * @param prefix Prefix to look for
 * @return true if str has prefix, false otherwise
 */
bool str_has_prefix(const char *str, const char *prefix);

/**
 * Convert string to lowercase (in-place)
 * @param str String to convert
 */
void str_to_lower(char *str);

/**
 * Split string by delimiter
 * @param str String to split
 * @param delimiter Delimiter character
 * @param parts Output array of string parts
 * @param max_parts Maximum number of parts
 * @return Number of parts found
 */
size_t str_split(const char *str, char delimiter, char **parts,
                 size_t max_parts);

/**
 * Check if string is a valid UR type
 * @param type Type string to validate
 * @return true if valid UR type, false otherwise
 */
bool is_ur_type(const char *type);

/**
 * Parse UR string into components
 * @param ur_str UR string to parse
 * @param type Output type string (allocated)
 * @param components Output components array (allocated)
 * @param component_count Output component count
 * @return true on success, false on error
 */
bool parse_ur_string(const char *ur_str, char **type, char ***components,
                     size_t *component_count);

/**
 * Parse sequence component (e.g., "1-5" -> seq_num=1, seq_len=5)
 * @param seq_str Sequence string
 * @param seq_num Output sequence number
 * @param seq_len Output sequence length
 * @return true on success, false on error
 */
bool parse_sequence_component(const char *seq_str, uint32_t *seq_num,
                              size_t *seq_len);

/**
 * Free string array
 * @param strings Array of strings
 * @param count Number of strings
 */
void free_string_array(char **strings, size_t count);

// Memory utilities

/**
 * Safe malloc with zero initialization
 * @param size Size to allocate
 * @return Allocated memory or NULL on error
 */
void *safe_malloc(size_t size);

/**
 * Safe malloc without zero initialization (for data buffers that will be
 * immediately overwritten)
 * @param size Size to allocate
 * @return Allocated memory or NULL on error
 */
void *safe_malloc_uninit(size_t size);

/**
 * Safe realloc
 * @param ptr Pointer to reallocate
 * @param size New size
 * @return Reallocated memory or NULL on error
 */
void *safe_realloc(void *ptr, size_t size);

/**
 * Safe string duplication
 * @param str String to duplicate
 * @return Duplicated string or NULL on error
 */
char *safe_strdup(const char *str);

#endif // UR_UTILS_H