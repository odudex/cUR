//
// fountain_decoder.c
//
// Copyright © 2025 Krux Contributors
// Licensed under the "BSD-2-Clause Plus Patent License"
//
// Implementation of fountain code decoder for multi-part data reassembly.
// Based on the specification:
// https://github.com/BlockchainCommons/Research/blob/master/papers/bcr-2020-006-urtypes.md
//
// This is an independent implementation written using
// foundation-ur-py as a reference for testing and validation.
//

#include "fountain_decoder.h"
#include "crc32.h"
#include "fountain_utils.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Configuration constants
#define QUEUE_INITIAL_CAPACITY 16
#define SIMPLE_PARTS_INITIAL_CAPACITY 8
#define INDEXES_INITIAL_CAPACITY 8
#define HASH_MIN_CAPACITY 64
#define HASH_CAPACITY_MULTIPLIER 1 // Reduced from 2 to save memory
#define MAX_MIXED_PARTS 256 // Limit mixed parts to prevent memory explosion
#define MAX_DUPLICATE_TRACKING 512 // Limit duplicate tracking set size
#define CROSS_REDUCTION_MAX_ITERATIONS 7
#define FNV1A_OFFSET_BASIS 2166136261u
#define FNV1A_PRIME 16777619u

// Hash table for mixed parts - key is the set of indexes
typedef struct hash_entry {
  part_indexes_t key;
  decoder_part_t value;
  size_t key_hash; // Cached hash for fast collision filtering
  struct hash_entry *next;
} hash_entry_t;

struct mixed_parts_hash {
  hash_entry_t **buckets;
  size_t count;
  size_t capacity;
};

// Hash function for part indexes using FNV-1a algorithm
static size_t hash_indexes(const part_indexes_t *indexes) {
  if (!indexes || indexes->count == 0)
    return 0;

  size_t hash = FNV1A_OFFSET_BASIS;
  for (size_t i = 0; i < indexes->count; i++) {
    hash ^= indexes->indexes[i];
    hash *= FNV1A_PRIME;
  }
  return hash;
}

// Hash set operations for lightweight duplicate detection

// Initialize hash set
static bool hash_set_init(hash_set_t *set, size_t capacity) {
  if (!set || capacity == 0)
    return false;

  set->hashes = safe_malloc(capacity * sizeof(uint32_t));
  if (!set->hashes)
    return false;

  set->count = 0;
  set->capacity = capacity;
  return true;
}

// Free hash set
static void hash_set_free(hash_set_t *set) {
  if (!set)
    return;

  if (set->hashes) {
    free(set->hashes);
    set->hashes = NULL;
  }
  set->count = 0;
  set->capacity = 0;
}

// Check if hash set contains a hash (computed from indexes)
static bool hash_set_contains(const hash_set_t *set, uint32_t hash) {
  if (!set || !set->hashes)
    return false;

  for (size_t i = 0; i < set->count; i++) {
    if (set->hashes[i] == hash) {
      return true;
    }
  }
  return false;
}

// Add hash to set (returns false if already exists or on error)
static bool hash_set_add(hash_set_t *set, uint32_t hash) {
  if (!set)
    return false;

  // Check if already exists
  if (hash_set_contains(set, hash)) {
    return false;
  }

  // Limit growth to prevent unbounded memory usage on embedded devices
  // When limit is reached, stop tracking duplicates (seq_num check handles
  // consecutive dupes)
  if (set->count >= MAX_DUPLICATE_TRACKING) {
    return false; // Limit reached, skip tracking this hash
  }

  // Expand if needed (but respect MAX_DUPLICATE_TRACKING limit)
  if (set->count >= set->capacity) {
    size_t new_capacity = set->capacity * 2;
    if (new_capacity > MAX_DUPLICATE_TRACKING) {
      new_capacity = MAX_DUPLICATE_TRACKING;
    }
    uint32_t *new_hashes =
        safe_realloc(set->hashes, new_capacity * sizeof(uint32_t));
    if (!new_hashes)
      return false;

    set->hashes = new_hashes;
    set->capacity = new_capacity;
  }

  set->hashes[set->count++] = hash;
  return true;
}

// Initialize hash table with dynamic capacity
static bool mixed_hash_init(mixed_parts_hash_t *hash, size_t capacity) {
  if (!hash || capacity == 0)
    return false;

  hash->buckets = calloc(capacity, sizeof(hash_entry_t *));
  if (!hash->buckets)
    return false;

  hash->count = 0;
  hash->capacity = capacity;
  return true;
}

// Free hash table
static void mixed_hash_free(mixed_parts_hash_t *hash) {
  if (!hash || !hash->buckets)
    return;

  for (size_t i = 0; i < hash->capacity; i++) {
    hash_entry_t *entry = hash->buckets[i];
    while (entry) {
      hash_entry_t *next = entry->next;
      decoder_part_free(&entry->value);
      if (entry->key.indexes) {
        free(entry->key.indexes);
      }
      free(entry);
      entry = next;
    }
  }

  free(hash->buckets);
  hash->buckets = NULL;
  hash->count = 0;
}

// Check if hash contains key
static bool mixed_hash_contains(const mixed_parts_hash_t *hash,
                                const part_indexes_t *key) {
  if (!hash || !hash->buckets || !key)
    return false;

  size_t key_hash = hash_indexes(key);
  size_t bucket = key_hash % hash->capacity;
  hash_entry_t *entry = hash->buckets[bucket];

  while (entry) {
    // Fast path: compare cached hash first
    if (entry->key_hash == key_hash && part_indexes_equal(&entry->key, key)) {
      return true;
    }
    entry = entry->next;
  }

  return false;
}

// Add or update entry in hash table
static bool mixed_hash_put(mixed_parts_hash_t *hash, const part_indexes_t *key,
                           const decoder_part_t *value) {
  if (!hash || !hash->buckets || !key || !value)
    return false;

  size_t key_hash = hash_indexes(key);
  size_t bucket = key_hash % hash->capacity;

  // Check if already exists
  hash_entry_t *entry = hash->buckets[bucket];
  while (entry) {
    // Fast path: compare cached hash first
    if (entry->key_hash == key_hash && part_indexes_equal(&entry->key, key)) {
      // Already exists, don't add duplicate
      return false;
    }
    entry = entry->next;
  }

  // Create new entry using calloc to zero-initialize
  // This is important: if malloc reuses freed memory, zeroed fields
  // help detect stale pointers (key.indexes will be NULL)
  hash_entry_t *new_entry = calloc(1, sizeof(hash_entry_t));
  if (!new_entry)
    return false;

  new_entry->key_hash = key_hash; // Cache the hash

  if (!part_indexes_copy(key, &new_entry->key)) {
    free(new_entry);
    return false;
  }

  if (!decoder_part_copy(value, &new_entry->value)) {
    if (new_entry->key.indexes) {
      free(new_entry->key.indexes);
    }
    free(new_entry);
    return false;
  }

  new_entry->next = hash->buckets[bucket];
  hash->buckets[bucket] = new_entry;
  hash->count++;

  return true;
}

bool queue_init(part_queue_t *queue, size_t capacity) {
  if (!queue || capacity == 0)
    return false;

  queue->parts = safe_malloc(capacity * sizeof(decoder_part_t));
  if (!queue->parts)
    return false;

  for (size_t i = 0; i < capacity; i++) {
    queue->parts[i].indexes.indexes = NULL;
    queue->parts[i].indexes.count = 0;
    queue->parts[i].indexes.capacity = 0;
    queue->parts[i].data = NULL;
    queue->parts[i].data_len = 0;
  }

  queue->front = 0;
  queue->rear = 0;
  queue->count = 0;
  queue->capacity = capacity;

  return true;
}

void queue_free(part_queue_t *queue) {
  if (!queue)
    return;

  if (queue->parts) {
    for (size_t i = 0; i < queue->count; i++) {
      size_t idx = (queue->front + i) % queue->capacity;
      decoder_part_free(&queue->parts[idx]);
    }
    free(queue->parts);
  }

  memset(queue, 0, sizeof(part_queue_t));
}

bool queue_enqueue(part_queue_t *queue, const decoder_part_t *part) {
  if (!queue || !part || queue->count >= queue->capacity)
    return false;

  if (!decoder_part_copy(part, &queue->parts[queue->rear])) {
    return false;
  }

  queue->rear = (queue->rear + 1) % queue->capacity;
  queue->count++;

  return true;
}

bool queue_dequeue(part_queue_t *queue, decoder_part_t *part) {
  if (!queue || !part || queue->count == 0)
    return false;

  if (!decoder_part_copy(&queue->parts[queue->front], part)) {
    return false;
  }

  decoder_part_free(&queue->parts[queue->front]);
  queue->front = (queue->front + 1) % queue->capacity;
  queue->count--;

  return true;
}

bool queue_is_empty(const part_queue_t *queue) {
  return !queue || queue->count == 0;
}

void decoder_part_free(decoder_part_t *part) {
  if (!part)
    return;

  if (part->indexes.indexes) {
    free(part->indexes.indexes);
    part->indexes.indexes = NULL;
  }
  part->indexes.count = 0;
  part->indexes.capacity = 0;

  if (part->data) {
    free(part->data);
    part->data = NULL;
  }
  part->data_len = 0;
}

bool decoder_part_copy(const decoder_part_t *src, decoder_part_t *dst) {
  if (!src || !dst)
    return false;

  decoder_part_free(dst);

  dst->indexes.indexes = NULL;
  dst->indexes.count = 0;
  dst->indexes.capacity = 0;

  if (!part_indexes_copy(&src->indexes, &dst->indexes)) {
    return false;
  }

  if (src->data && src->data_len > 0) {
    dst->data = safe_malloc(src->data_len);
    if (!dst->data) {
      part_indexes_free(&dst->indexes);
      return false;
    }
    memcpy(dst->data, src->data, src->data_len);
    dst->data_len = src->data_len;
  } else {
    dst->data = NULL;
    dst->data_len = 0;
  }

  return true;
}

part_indexes_t *part_indexes_new(void) {
  part_indexes_t *indexes = safe_malloc(sizeof(part_indexes_t));
  if (indexes) {
    indexes->indexes = NULL;
    indexes->count = 0;
    indexes->capacity = 0;
  }
  return indexes;
}

void part_indexes_free(part_indexes_t *indexes) {
  if (indexes) {
    if (indexes->indexes) {
      free(indexes->indexes);
    }
    free(indexes);
  }
}

bool part_indexes_add(part_indexes_t *indexes, size_t index) {
  if (!indexes)
    return false;

  if (part_indexes_contains(indexes, index)) {
    return true;
  }

  if (indexes->count >= indexes->capacity) {
    size_t new_capacity = indexes->capacity == 0 ? INDEXES_INITIAL_CAPACITY
                                                 : indexes->capacity * 2;
    size_t *new_indexes =
        safe_realloc(indexes->indexes, sizeof(size_t) * new_capacity);
    if (!new_indexes)
      return false;

    indexes->indexes = new_indexes;
    indexes->capacity = new_capacity;
  }

  indexes->indexes[indexes->count++] = index;
  return true;
}

bool part_indexes_contains(const part_indexes_t *indexes, size_t index) {
  if (!indexes)
    return false;

  for (size_t i = 0; i < indexes->count; i++) {
    if (indexes->indexes[i] == index) {
      return true;
    }
  }
  return false;
}

void part_indexes_clear(part_indexes_t *indexes) {
  if (indexes) {
    indexes->count = 0;
  }
}

fountain_decoder_t *fountain_decoder_new(void) {
  fountain_decoder_t *decoder = safe_malloc(sizeof(fountain_decoder_t));
  if (!decoder)
    return NULL;

  decoder->received_part_indexes.indexes = NULL;
  decoder->received_part_indexes.count = 0;
  decoder->received_part_indexes.capacity = 0;

  decoder->last_part_indexes = NULL;
  decoder->processed_parts_count = 0;
  decoder->result = NULL;
  decoder->expected_part_indexes = NULL;
  decoder->expected_fragment_len = 0;
  decoder->expected_message_len = 0;
  decoder->expected_checksum = 0;

  decoder->simple_parts.keys = NULL;
  decoder->simple_parts.values = NULL;
  decoder->simple_parts.value_lens = NULL;
  decoder->simple_parts.count = 0;
  decoder->simple_parts.capacity = 0;

  // Hash tables will be initialized when we know seq_len
  decoder->mixed_parts_hash = NULL;

  // Initialize hash set for duplicate detection (will resize when we know
  // seq_len)
  decoder->received_fragments_hashes.hashes = NULL;
  decoder->received_fragments_hashes.count = 0;
  decoder->received_fragments_hashes.capacity = 0;

  if (!queue_init(&decoder->queue, QUEUE_INITIAL_CAPACITY)) {
    free(decoder);
    return NULL;
  }

  decoder->last_fragment_seq_num = 0;
  decoder->has_received_fragment = false;

#ifdef DEBUG_STATS
  decoder->maximum_mixed_parts = 0;
  decoder->mixed_from_fragments = 0;
  decoder->mixed_from_reduction = 0;
  decoder->mixed_from_cross_reduction = 0;
  decoder->mixed_parts_useful = 0;
#endif

  return decoder;
}

void fountain_decoder_free(fountain_decoder_t *decoder) {
  if (!decoder)
    return;

  if (decoder->received_part_indexes.indexes) {
    free(decoder->received_part_indexes.indexes);
  }

  if (decoder->last_part_indexes) {
    part_indexes_free(decoder->last_part_indexes);
  }

  if (decoder->expected_part_indexes) {
    part_indexes_free(decoder->expected_part_indexes);
  }

  if (decoder->result) {
    if (decoder->result->data) {
      free(decoder->result->data);
    }
    free(decoder->result);
  }

  if (decoder->simple_parts.keys)
    free(decoder->simple_parts.keys);
  if (decoder->simple_parts.value_lens)
    free(decoder->simple_parts.value_lens);
  if (decoder->simple_parts.values) {
    for (size_t i = 0; i < decoder->simple_parts.count; i++) {
      decoder_part_free(&decoder->simple_parts.values[i]);
    }
    free(decoder->simple_parts.values);
  }

  // Free hash table for mixed parts
  if (decoder->mixed_parts_hash) {
    mixed_hash_free(decoder->mixed_parts_hash);
    free(decoder->mixed_parts_hash);
    decoder->mixed_parts_hash = NULL;
  }

  // Free hash set for duplicate detection
  hash_set_free(&decoder->received_fragments_hashes);

  queue_free(&decoder->queue);

  free(decoder);
}

static bool create_decoder_part_from_encoder_part(
    const fountain_encoder_part_t *const encoder_part,
    decoder_part_t *const decoder_part) {
  if (!encoder_part || !decoder_part) {
    return false;
  }

  decoder_part->indexes.indexes = NULL;
  decoder_part->indexes.count = 0;
  decoder_part->indexes.capacity = 0;
  decoder_part->data = NULL;
  decoder_part->data_len = 0;

  if (!choose_fragments(encoder_part->seq_num, encoder_part->seq_len,
                        encoder_part->checksum, &decoder_part->indexes)) {
    return false;
  }

  if (encoder_part->data && encoder_part->data_len > 0) {
    decoder_part->data = safe_malloc(encoder_part->data_len);
    if (!decoder_part->data) {
      if (decoder_part->indexes.indexes) {
        free(decoder_part->indexes.indexes);
        decoder_part->indexes.indexes = NULL;
      }
      return false;
    }
    memcpy(decoder_part->data, encoder_part->data, encoder_part->data_len);
    decoder_part->data_len = encoder_part->data_len;
  }

  return true;
}

typedef enum {
  MIXED_SOURCE_FRAGMENT,
  MIXED_SOURCE_REDUCTION,
  MIXED_SOURCE_CROSS_REDUCTION
} mixed_part_source_t;

static int compare_fragment_info(const void *a, const void *b) {
  const struct {
    size_t index;
    uint8_t *data;
    size_t len;
  } *fa = a, *fb = b;

  if (fa->index < fb->index)
    return -1;
  if (fa->index > fb->index)
    return 1;
  return 0;
}

static bool is_simple_part(const decoder_part_t *const part) {
  return part && part->indexes.count == 1;
}

static size_t get_part_index(const decoder_part_t *const part) {
  if (!part || part->indexes.count == 0)
    return 0;
  return part->indexes.indexes[0];
}

static bool add_simple_part(fountain_decoder_t *const decoder,
                            const decoder_part_t *const part) {
  if (!decoder || !part || !is_simple_part(part))
    return false;

  size_t index = get_part_index(part);

  for (size_t i = 0; i < decoder->simple_parts.count; i++) {
    if (decoder->simple_parts.keys[i] == index) {
      return true;
    }
  }

  if (decoder->simple_parts.count >= decoder->simple_parts.capacity) {
    size_t new_capacity = decoder->simple_parts.capacity == 0
                              ? SIMPLE_PARTS_INITIAL_CAPACITY
                              : decoder->simple_parts.capacity * 2;

    size_t *new_keys =
        safe_realloc(decoder->simple_parts.keys, sizeof(size_t) * new_capacity);
    decoder_part_t *new_values = safe_realloc(
        decoder->simple_parts.values, sizeof(decoder_part_t) * new_capacity);
    size_t *new_lens = safe_realloc(decoder->simple_parts.value_lens,
                                    sizeof(size_t) * new_capacity);

    if (!new_keys || !new_values || !new_lens) {
      return false;
    }

    for (size_t i = decoder->simple_parts.capacity; i < new_capacity; i++) {
      new_values[i].indexes.indexes = NULL;
      new_values[i].indexes.count = 0;
      new_values[i].indexes.capacity = 0;
      new_values[i].data = NULL;
      new_values[i].data_len = 0;
    }

    decoder->simple_parts.keys = new_keys;
    decoder->simple_parts.values = new_values;
    decoder->simple_parts.value_lens = new_lens;
    decoder->simple_parts.capacity = new_capacity;
  }

  decoder_part_t *stored_part =
      &decoder->simple_parts.values[decoder->simple_parts.count];
  if (!decoder_part_copy(part, stored_part)) {
    return false;
  }

  decoder->simple_parts.keys[decoder->simple_parts.count] = index;
  decoder->simple_parts.value_lens[decoder->simple_parts.count] =
      part->data_len;
  decoder->simple_parts.count++;

  return true;
}

static bool reduce_part_by_part(const decoder_part_t *const a,
                                const decoder_part_t *const b,
                                decoder_part_t *const result) {
  if (!a || !b || !result)
    return false;

  // Only reduce if b's indexes are a strict subset of a's indexes
  if (!part_indexes_is_strict_subset(&b->indexes, &a->indexes)) {
    return decoder_part_copy(a, result);
  }

  result->indexes.indexes = NULL;
  result->indexes.count = 0;
  result->indexes.capacity = 0;
  result->data = NULL;
  result->data_len = 0;

  if (!part_indexes_difference(&a->indexes, &b->indexes, &result->indexes)) {
    return false;
  }

  result->data = safe_malloc(a->data_len);
  if (!result->data) {
    if (result->indexes.indexes) {
      free(result->indexes.indexes);
    }
    return false;
  }

  result->data_len = a->data_len;
  for (size_t i = 0; i < a->data_len; i++) {
    result->data[i] = a->data[i] ^ b->data[i];
  }
  return true;
}

static bool add_mixed_part(fountain_decoder_t *const decoder,
                           const decoder_part_t *const part,
                           const mixed_part_source_t source) {
  if (!decoder || !part || is_simple_part(part) || !decoder->mixed_parts_hash)
    return false;

  // Limit mixed parts to prevent memory explosion on embedded devices
  // When limit is reached, skip adding new mixed parts but continue processing
  if (decoder->mixed_parts_hash->count >= MAX_MIXED_PARTS) {
    return false; // Limit reached, skip adding this mixed part
  }

  // Try to add to hash table (which automatically checks for duplicates)
  if (!mixed_hash_put(decoder->mixed_parts_hash, &part->indexes, part)) {
    return false; // Duplicate or error
  }

#ifdef DEBUG_STATS
  // Track the source of this mixed part
  switch (source) {
  case MIXED_SOURCE_FRAGMENT:
    decoder->mixed_from_fragments++;
    break;
  case MIXED_SOURCE_REDUCTION:
    decoder->mixed_from_reduction++;
    break;
  case MIXED_SOURCE_CROSS_REDUCTION:
    decoder->mixed_from_cross_reduction++;
    break;
  }

  // Update maximum tracking
  if (decoder->mixed_parts_hash->count > decoder->maximum_mixed_parts) {
    decoder->maximum_mixed_parts = decoder->mixed_parts_hash->count;
  }
#endif
  (void)source; // Suppress unused parameter warning when DEBUG_STATS is not
                // defined

  return true;
}

static void reduce_mixed_by(fountain_decoder_t *const decoder,
                            const decoder_part_t *const part) {
  if (!decoder || !part || !decoder->mixed_parts_hash ||
      decoder->mixed_parts_hash->count == 0)
    return;

  mixed_parts_hash_t *hash = decoder->mixed_parts_hash;

  // Collect entries that need removal (simple parts) or rehashing (bucket
  // changed) We can't modify the hash table while iterating, so collect first
  size_t max_entries = hash->count;
  hash_entry_t **to_remove = safe_malloc(max_entries * sizeof(hash_entry_t *));
  size_t remove_count = 0;

  // Track entries that need rehashing (their key changed, so bucket may change)
  hash_entry_t **to_rehash = safe_malloc(max_entries * sizeof(hash_entry_t *));
  size_t *old_buckets = safe_malloc(max_entries * sizeof(size_t));
  size_t rehash_count = 0;

  if (!to_remove || !to_rehash || !old_buckets) {
    free(to_remove);
    free(to_rehash);
    free(old_buckets);
    return;
  }

  // First pass: reduce entries in-place, mark for removal or rehashing
  for (size_t i = 0; i < hash->capacity; i++) {
    hash_entry_t *entry = hash->buckets[i];
    while (entry) {
      hash_entry_t *next =
          entry->next; // Save next before potential modification

      // Check if this entry can be reduced by the given part
      if (part_indexes_is_strict_subset(&part->indexes, &entry->key)) {
        // Reduce the entry in-place
        part_indexes_t new_indexes = {0};
        if (!part_indexes_difference(&entry->key, &part->indexes,
                                     &new_indexes)) {
          entry = next;
          continue;
        }

        // XOR the data in-place
        for (size_t j = 0; j < entry->value.data_len && j < part->data_len;
             j++) {
          entry->value.data[j] ^= part->data[j];
        }

        // Update the indexes (free old, assign new)
        if (entry->key.indexes) {
          free(entry->key.indexes);
        }
        entry->key = new_indexes;

        // Also update the value's indexes
        if (entry->value.indexes.indexes) {
          free(entry->value.indexes.indexes);
        }
        entry->value.indexes.indexes = NULL;
        entry->value.indexes.count = 0;
        entry->value.indexes.capacity = 0;
        part_indexes_copy(&new_indexes, &entry->value.indexes);

        if (is_simple_part(&entry->value)) {
          // Mark for removal and queue
#ifdef DEBUG_STATS
          decoder->mixed_parts_useful++;
#endif
          size_t fragment_idx = get_part_index(&entry->value);
          if (!part_indexes_contains(&decoder->received_part_indexes,
                                     fragment_idx)) {
            queue_enqueue(&decoder->queue, &entry->value);
          }
          to_remove[remove_count++] = entry;
        } else {
          // Check if bucket changed (key hash changed)
          size_t new_bucket = hash_indexes(&entry->key) % hash->capacity;
          if (new_bucket != i) {
            // Entry needs to move to a different bucket
            to_rehash[rehash_count] = entry;
            old_buckets[rehash_count] = i;
            rehash_count++;
          }
        }
      }

      entry = next;
    }
  }

  // Second pass: remove entries that became simple parts
  for (size_t i = 0; i < remove_count; i++) {
    hash_entry_t *entry = to_remove[i];
    size_t bucket = hash_indexes(&entry->key) % hash->capacity;

    // Find and unlink from bucket chain
    hash_entry_t *curr = hash->buckets[bucket];
    hash_entry_t *prev = NULL;
    while (curr) {
      if (curr == entry) {
        if (prev) {
          prev->next = curr->next;
        } else {
          hash->buckets[bucket] = curr->next;
        }
        // Free the entry
        decoder_part_free(&entry->value);
        if (entry->key.indexes) {
          free(entry->key.indexes);
        }
        free(entry);
        hash->count--;
        break;
      }
      prev = curr;
      curr = curr->next;
    }
  }

  // Third pass: rehash entries that changed buckets
  for (size_t i = 0; i < rehash_count; i++) {
    hash_entry_t *entry = to_rehash[i];
    size_t old_bucket = old_buckets[i];
    size_t new_bucket = hash_indexes(&entry->key) % hash->capacity;

    // Skip if entry was already removed (became simple part)
    bool was_removed = false;
    for (size_t j = 0; j < remove_count; j++) {
      if (to_remove[j] == entry) {
        was_removed = true;
        break;
      }
    }
    if (was_removed)
      continue;

    // Unlink from old bucket
    hash_entry_t *curr = hash->buckets[old_bucket];
    hash_entry_t *prev = NULL;
    while (curr) {
      if (curr == entry) {
        if (prev) {
          prev->next = curr->next;
        } else {
          hash->buckets[old_bucket] = curr->next;
        }
        break;
      }
      prev = curr;
      curr = curr->next;
    }

    // Insert into new bucket
    entry->next = hash->buckets[new_bucket];
    hash->buckets[new_bucket] = entry;
  }

  free(to_remove);
  free(to_rehash);
  free(old_buckets);

#ifdef DEBUG_STATS
  if (hash->count > decoder->maximum_mixed_parts) {
    decoder->maximum_mixed_parts = hash->count;
  }
#endif
}

static bool create_symmetric_diff(const decoder_part_t *const a,
                                  const decoder_part_t *const b,
                                  decoder_part_t *const result) {
  if (!a || !b || !result || a->data_len != b->data_len)
    return false;

  result->indexes.indexes = NULL;
  result->indexes.count = 0;
  result->indexes.capacity = 0;
  result->data = NULL;
  result->data_len = 0;

  if (!part_indexes_symmetric_difference(&a->indexes, &b->indexes,
                                         &result->indexes)) {
    return false;
  }

  if (result->indexes.count == 0) {
    if (result->indexes.indexes) {
      free(result->indexes.indexes);
      result->indexes.indexes = NULL;
    }
    return false;
  }

  result->data = safe_malloc(a->data_len);
  if (!result->data) {
    if (result->indexes.indexes) {
      free(result->indexes.indexes);
      result->indexes.indexes = NULL;
    }
    result->indexes.count = 0;
    result->indexes.capacity = 0;
    return false;
  }

  result->data_len = a->data_len;
  for (size_t i = 0; i < a->data_len; i++) {
    result->data[i] = a->data[i] ^ b->data[i];
  }

  return true;
}

static void gaussian_reduce_with_new_part(fountain_decoder_t *const decoder,
                                          const decoder_part_t *const pivot);

static void reduce_mixed_against_mixed(fountain_decoder_t *const decoder) {
  if (!decoder || !decoder->mixed_parts_hash ||
      decoder->mixed_parts_hash->count < 2) {
    return;
  }

  bool made_progress = true;
  int iteration = 0;

  while (made_progress && iteration < CROSS_REDUCTION_MAX_ITERATIONS) {
    made_progress = false;
    iteration++;

    // Collect all current hash entries into an array for iteration
    size_t entry_count = decoder->mixed_parts_hash->count;
    hash_entry_t **entries = safe_malloc(sizeof(hash_entry_t *) * entry_count);
    if (!entries)
      return;

    size_t idx = 0;
    for (size_t b = 0;
         b < decoder->mixed_parts_hash->capacity && idx < entry_count; b++) {
      hash_entry_t *entry = decoder->mixed_parts_hash->buckets[b];
      while (entry && idx < entry_count) {
        entries[idx++] = entry;
        entry = entry->next;
      }
    }

    for (size_t i = 0; i < entry_count && !made_progress; i++) {
      for (size_t offset = 1;
           offset <= entry_count && (i + offset) < entry_count; offset++) {
        size_t j = i + offset;

        if (part_indexes_have_intersection(&entries[i]->key,
                                           &entries[j]->key)) {

          decoder_part_t new_part = {0};

          if (create_symmetric_diff(&entries[i]->value, &entries[j]->value,
                                    &new_part)) {

            bool is_simpler = new_part.indexes.count < entries[i]->key.count ||
                              new_part.indexes.count < entries[j]->key.count;

            if (!is_simpler) {
              decoder_part_free(&new_part);
              continue;
            }

            // Hash table automatically checks for duplicates in add_mixed_part
            if (new_part.indexes.count > 0) {
              if (is_simple_part(&new_part)) {
#ifdef DEBUG_STATS
                decoder->mixed_parts_useful++; // Cross-reduction led to simple
                                               // part!
#endif
                size_t fragment_idx = get_part_index(&new_part);
                if (!part_indexes_contains(&decoder->received_part_indexes,
                                           fragment_idx)) {
                  queue_enqueue(&decoder->queue, &new_part);
                  made_progress = true;
                }
                decoder_part_free(&new_part);
                break;
              } else {
                if (add_mixed_part(decoder, &new_part,
                                   MIXED_SOURCE_CROSS_REDUCTION)) {
                  made_progress = true;
                  gaussian_reduce_with_new_part(decoder, &new_part);
                }
                decoder_part_free(&new_part);
              }
              break;
            } else {
              decoder_part_free(&new_part);
            }
          }
        }
      }
    }

    free(entries);
  }
}

static void gaussian_reduce_with_new_part(fountain_decoder_t *const decoder,
                                          const decoder_part_t *const pivot) {
  if (!decoder || !pivot || is_simple_part(pivot) || !decoder->mixed_parts_hash)
    return;

  // We iterate over buckets directly to avoid stale pointer issues.
  // When we remove an entry and add a new one, we need to be careful:
  // - The new entry goes to the HEAD of its bucket
  // - We only continue with 'next' which was valid before the modification
  for (size_t b = 0; b < decoder->mixed_parts_hash->capacity; b++) {
    hash_entry_t *entry = decoder->mixed_parts_hash->buckets[b];
    hash_entry_t *prev = NULL;

    while (entry) {
      // Save next pointer BEFORE any potential modification
      hash_entry_t *next = entry->next;

      // Skip if this is the pivot itself
      if (part_indexes_equal(&entry->key, &pivot->indexes)) {
        prev = entry;
        entry = next;
        continue;
      }

      if (part_indexes_is_strict_subset(&pivot->indexes, &entry->key)) {
        decoder_part_t reduced = {0};
        reduced.indexes.indexes = NULL;
        reduced.indexes.count = 0;
        reduced.indexes.capacity = 0;
        reduced.data = NULL;
        reduced.data_len = 0;

        if (reduce_part_by_part(&entry->value, pivot, &reduced)) {
          // Unlink entry from bucket chain BEFORE freeing
          if (prev) {
            prev->next = next;
          } else {
            decoder->mixed_parts_hash->buckets[b] = next;
          }

          // Free the entry
          decoder_part_free(&entry->value);
          if (entry->key.indexes) {
            free(entry->key.indexes);
          }
          free(entry);
          decoder->mixed_parts_hash->count--;

          // Handle reduced part
          if (is_simple_part(&reduced)) {
            size_t fragment_idx = get_part_index(&reduced);
            if (!part_indexes_contains(&decoder->received_part_indexes,
                                       fragment_idx)) {
              queue_enqueue(&decoder->queue, &reduced);
            }
          } else {
            // Add reduced mixed part back
            add_mixed_part(decoder, &reduced, MIXED_SOURCE_REDUCTION);
          }
          decoder_part_free(&reduced);

          // Don't update prev - it still points to the previous valid entry
          // (or is NULL if we removed the head)
          entry = next;
          continue;
        }
      }

      prev = entry;
      entry = next;
    }
  }
}

static void process_simple_part(fountain_decoder_t *const decoder,
                                const decoder_part_t *const part) {
  if (!decoder || !part || !is_simple_part(part))
    return;

  size_t fragment_index = get_part_index(part);

  if (part_indexes_contains(&decoder->received_part_indexes, fragment_index)) {
    return;
  }

  if (!add_simple_part(decoder, part)) {
    return;
  }

  if (!part_indexes_add(&decoder->received_part_indexes, fragment_index)) {
    return;
  }

  if (decoder->expected_part_indexes &&
      part_indexes_equal(&decoder->received_part_indexes,
                         decoder->expected_part_indexes)) {

    size_t part_count = decoder->simple_parts.count;
    uint8_t **fragments = safe_malloc(part_count * sizeof(uint8_t *));
    size_t *fragment_lens = safe_malloc(part_count * sizeof(size_t));

    if (!fragments || !fragment_lens) {
      if (fragments)
        free(fragments);
      if (fragment_lens)
        free(fragment_lens);
      return;
    }

    typedef struct {
      size_t index;
      uint8_t *data;
      size_t len;
    } fragment_info_t;

    fragment_info_t *sorted_fragments =
        safe_malloc(part_count * sizeof(fragment_info_t));
    if (!sorted_fragments) {
      free(fragments);
      free(fragment_lens);
      return;
    }

    for (size_t i = 0; i < part_count; i++) {
      sorted_fragments[i].index = decoder->simple_parts.keys[i];
      sorted_fragments[i].data = decoder->simple_parts.values[i].data;
      sorted_fragments[i].len = decoder->simple_parts.values[i].data_len;
    }

    // Sort fragments by index using qsort
    qsort(sorted_fragments, part_count, sizeof(fragment_info_t),
          compare_fragment_info);

    for (size_t i = 0; i < part_count; i++) {
      fragments[i] = sorted_fragments[i].data;
      fragment_lens[i] = sorted_fragments[i].len;
    }

    free(sorted_fragments);

    uint8_t *message = safe_malloc(decoder->expected_message_len);
    if (!message) {
      free(fragments);
      free(fragment_lens);
      return;
    }

    if (join_fragments(fragments, fragment_lens, part_count,
                       decoder->expected_message_len, message)) {
      uint32_t checksum =
          crc32_calculate(message, decoder->expected_message_len);

      if (checksum == decoder->expected_checksum) {
        decoder->result = safe_malloc(sizeof(fountain_decoder_result_t));
        if (decoder->result) {
          decoder->result->data = message;
          decoder->result->data_len = decoder->expected_message_len;
          decoder->result->is_success = true;
          decoder->result->is_error = false;
          message = NULL;
        }
#ifdef DEBUG_STATS
        printf("=== Mixed Parts Statistics ===\n");
        printf("Maximum mixed parts reached: %zu\n",
               decoder->maximum_mixed_parts);
        printf("  From fragments: %zu\n", decoder->mixed_from_fragments);
        printf("  From reduction: %zu\n", decoder->mixed_from_reduction);
        printf("  From cross-reduction: %zu\n",
               decoder->mixed_from_cross_reduction);
        printf("  Total created: %zu\n",
               decoder->mixed_from_fragments + decoder->mixed_from_reduction +
                   decoder->mixed_from_cross_reduction);
        printf("Mixed parts that were useful: %zu\n",
               decoder->mixed_parts_useful);
#endif
      } else {
        decoder->result = safe_malloc(sizeof(fountain_decoder_result_t));
        if (decoder->result) {
          decoder->result->data = NULL;
          decoder->result->data_len = 0;
          decoder->result->is_success = false;
          decoder->result->is_error = true;
        }
      }
    }

    if (message)
      free(message);
    free(fragments);
    free(fragment_lens);
  }
}

static void process_mixed_part(fountain_decoder_t *const decoder,
                               const decoder_part_t *const part) {
  if (!decoder || !part || is_simple_part(part) || !decoder->mixed_parts_hash)
    return;

  // Check if this part already exists in the hash table
  if (mixed_hash_contains(decoder->mixed_parts_hash, &part->indexes)) {
    return;
  }

  decoder_part_t reduced_part = {0};
  reduced_part.indexes.indexes = NULL;
  reduced_part.indexes.count = 0;
  reduced_part.indexes.capacity = 0;
  reduced_part.data = NULL;
  reduced_part.data_len = 0;

  if (!decoder_part_copy(part, &reduced_part)) {
    return;
  }

  // Reduce by all simple parts
  for (size_t i = 0; i < decoder->simple_parts.count; i++) {
    decoder_part_t temp = {0};

    if (reduce_part_by_part(&reduced_part, &decoder->simple_parts.values[i],
                            &temp)) {
      decoder_part_free(&reduced_part);
      reduced_part = temp;
    }
  }

  // Reduce by all mixed parts in hash table
  for (size_t i = 0; i < decoder->mixed_parts_hash->capacity; i++) {
    hash_entry_t *entry = decoder->mixed_parts_hash->buckets[i];
    while (entry) {
      decoder_part_t temp = {0};

      if (reduce_part_by_part(&reduced_part, &entry->value, &temp)) {
        decoder_part_free(&reduced_part);
        reduced_part = temp;
      }
      entry = entry->next;
    }
  }

  if (is_simple_part(&reduced_part)) {
    queue_enqueue(&decoder->queue, &reduced_part);
  } else {
    add_mixed_part(decoder, &reduced_part, MIXED_SOURCE_FRAGMENT);
  }

  decoder_part_free(&reduced_part);
}

static void process_queue_item(fountain_decoder_t *const decoder) {
  if (!decoder || queue_is_empty(&decoder->queue))
    return;

  decoder_part_t part = {0};
  part.indexes.indexes = NULL;
  part.indexes.count = 0;
  part.indexes.capacity = 0;
  part.data = NULL;
  part.data_len = 0;

  if (!queue_dequeue(&decoder->queue, &part))
    return;

  if (is_simple_part(&part)) {
    process_simple_part(decoder, &part);
    reduce_mixed_by(decoder, &part);
    reduce_mixed_against_mixed(decoder);
  } else {
    process_mixed_part(decoder, &part);
    reduce_mixed_against_mixed(decoder);
  }

  decoder_part_free(&part);
}

bool fountain_decoder_receive_part(fountain_decoder_t *decoder,
                                   fountain_encoder_part_t *part) {
  if (!decoder || !part) {
    return false;
  }

  if (fountain_decoder_is_complete(decoder)) {
    return false;
  }

  if (decoder->has_received_fragment &&
      part->seq_num == decoder->last_fragment_seq_num) {
    return true;
  }

  if (decoder->expected_part_indexes == NULL) {
    decoder->expected_part_indexes = part_indexes_new();
    if (!decoder->expected_part_indexes)
      return false;

    for (size_t i = 0; i < part->seq_len; i++) {
      part_indexes_add(decoder->expected_part_indexes, i);
    }

    decoder->expected_checksum = part->checksum;
    decoder->expected_fragment_len = part->data_len;
    decoder->expected_message_len = part->message_len;

    // Initialize hash table with dynamic size based on seq_len
    //
    // Hash capacity heuristic rationale:
    // 1. Mixed parts are created from fragments with degree > 1 (multiple
    // indexes)
    // 2. In the worst case, we might store up to ~seq_len mixed parts
    // simultaneously
    // 3. Using seq_len * 2 as capacity provides:
    //    - Load factor of ~0.5 at maximum occupancy (good hash performance)
    //    - Reduces collision chains in the hash table
    //    - Balance between memory usage and lookup speed
    // 4. Minimum of HASH_MIN_CAPACITY (64) for small messages ensures
    // reasonable
    //    performance even when seq_len is very small (e.g., < 32 fragments)
    // 5. Note: Actual mixed parts count is typically much lower than seq_len
    //    due to Gaussian elimination reducing them to simple parts
    size_t hash_capacity =
        part->seq_len < (HASH_MIN_CAPACITY / HASH_CAPACITY_MULTIPLIER)
            ? HASH_MIN_CAPACITY
            : part->seq_len * HASH_CAPACITY_MULTIPLIER;

    decoder->mixed_parts_hash = safe_malloc(sizeof(mixed_parts_hash_t));
    if (!decoder->mixed_parts_hash)
      return false;

    if (!mixed_hash_init(decoder->mixed_parts_hash, hash_capacity)) {
      free(decoder->mixed_parts_hash);
      decoder->mixed_parts_hash = NULL;
      return false;
    }

    // Initialize lightweight hash set for duplicate detection
    // Uses much less memory than hash table (only stores hashes, not full
    // parts)
    if (!hash_set_init(&decoder->received_fragments_hashes, hash_capacity)) {
      mixed_hash_free(decoder->mixed_parts_hash);
      free(decoder->mixed_parts_hash);
      decoder->mixed_parts_hash = NULL;
      return false;
    }
  }

  decoder_part_t decoder_part;
  if (!create_decoder_part_from_encoder_part(part, &decoder_part)) {
    return false;
  }

  // Check if this exact fragment (by index set) has already been received
  // This prevents duplicate fragments from inflating processed_parts_count
  // Note: We track ALL received fragments, even after they're reduced
  // We compute a hash of the indexes and store only that (much more memory
  // efficient)
  uint32_t fragment_hash = (uint32_t)hash_indexes(&decoder_part.indexes);
  if (hash_set_contains(&decoder->received_fragments_hashes, fragment_hash)) {
    decoder_part_free(&decoder_part);
    return true; // Already received this exact fragment, skip it
  }

  // Mark this fragment as received by adding its hash to our tracking set
  // This persists even if the fragment gets reduced later
  // Only stores 4 bytes per fragment instead of full part data + indexes
  hash_set_add(&decoder->received_fragments_hashes, fragment_hash);

  if (decoder->last_part_indexes) {
    part_indexes_free(decoder->last_part_indexes);
  }
  decoder->last_part_indexes = part_indexes_new();
  if (decoder->last_part_indexes) {
    part_indexes_copy(&decoder_part.indexes, decoder->last_part_indexes);
  }

  if (!queue_enqueue(&decoder->queue, &decoder_part)) {
    decoder_part_free(&decoder_part);
    return false;
  }

  while (!fountain_decoder_is_complete(decoder) &&
         !queue_is_empty(&decoder->queue)) {
    process_queue_item(decoder);
  }

  decoder->processed_parts_count++;
  decoder->last_fragment_seq_num = part->seq_num;
  decoder->has_received_fragment = true;

  decoder_part_free(&decoder_part);

  return true;
}

bool fountain_decoder_is_complete(fountain_decoder_t *decoder) {
  return decoder && decoder->result != NULL;
}

bool fountain_decoder_is_success(fountain_decoder_t *decoder) {
  return decoder && decoder->result && decoder->result->is_success;
}

bool fountain_decoder_is_failure(fountain_decoder_t *decoder) {
  return decoder && decoder->result && decoder->result->is_error;
}

size_t fountain_decoder_expected_part_count(fountain_decoder_t *decoder) {
  if (!decoder || !decoder->expected_part_indexes)
    return 0;
  return decoder->expected_part_indexes->count;
}

double
fountain_decoder_estimated_percent_complete(fountain_decoder_t *decoder) {
  if (!decoder)
    return 0.0;
  if (fountain_decoder_is_complete(decoder))
    return 1.0;
  if (!decoder->expected_part_indexes)
    return 0.0;

  double estimated_input_parts =
      fountain_decoder_expected_part_count(decoder) * 1.75;
  double progress =
      (double)decoder->processed_parts_count / estimated_input_parts;
  return progress > 0.99 ? 0.99 : progress;
}

uint8_t *fountain_decoder_result_message(fountain_decoder_t *decoder) {
  if (!decoder || !decoder->result)
    return NULL;
  return decoder->result->data;
}

size_t fountain_decoder_result_message_len(fountain_decoder_t *decoder) {
  if (!decoder || !decoder->result)
    return 0;
  return decoder->result->data_len;
}
