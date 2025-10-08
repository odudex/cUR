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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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
  if (!type || strlen(type) == 0)
    return false;

  if (type[0] == '-' || type[strlen(type) - 1] == '-')
    return false;

  for (const char *p = type; *p; p++) {
    if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) &&
        *p != '-') {
      return false;
    }
  }

  return true;
}

bool parse_ur_string(const char *ur_str, char **type, char ***components,
                     size_t *component_count) {
  if (!ur_str || !type || !components || !component_count)
    return false;

  size_t len = strlen(ur_str);
  char *lowered = safe_malloc(len + 1);
  if (!lowered)
    return false;

  strcpy(lowered, ur_str);
  str_to_lower(lowered);

  if (!str_has_prefix(lowered, "ur:")) {
    free(lowered);
    return false;
  }

  char *path = lowered + 3;
  if (strlen(path) == 0) {
    free(lowered);
    return false;
  }

  char **parts = safe_malloc(sizeof(char *) * 10);
  if (!parts) {
    free(lowered);
    return false;
  }

  size_t part_count = str_split(path, '/', parts, 10);
  if (part_count < 2) {
    free_string_array(parts, part_count);
    free(parts);
    free(lowered);
    return false;
  }

  if (!is_ur_type(parts[0])) {
    free_string_array(parts, part_count);
    free(parts);
    free(lowered);
    return false;
  }

  *type = safe_strdup(parts[0]);
  *component_count = part_count - 1;
  *components = safe_malloc(sizeof(char *) * (*component_count));

  if (!*type || !*components) {
    if (*type)
      free(*type);
    if (*components)
      free(*components);
    free_string_array(parts, part_count);
    free(parts);
    free(lowered);
    return false;
  }

  for (size_t i = 0; i < *component_count; i++) {
    (*components)[i] = safe_strdup(parts[i + 1]);
    if (!(*components)[i]) {
      free(*type);
      free_string_array(*components, i);
      free(*components);
      free_string_array(parts, part_count);
      free(lowered);
      return false;
    }
  }

  free_string_array(parts, part_count);
  free(parts);
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
  void *ptr = malloc(size);
  if (ptr) {
    memset(ptr, 0, size);
  }
  return ptr;
}

void *safe_realloc(void *ptr, size_t size) { return realloc(ptr, size); }

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