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
#include "xor_internal.h"
#ifdef DEBUG_STATS
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>

// Forward declarations
typedef struct mixed_parts_hash mixed_parts_hash_t;

// Lightweight hash set for duplicate detection (stores only hashes)
typedef struct {
  uint32_t *hashes;
  size_t count;
  size_t capacity;
} hash_set_t;

// Queue for processing parts
typedef struct {
  decoder_part_t *parts;
  size_t front;
  size_t rear;
  size_t count;
  size_t capacity;
} part_queue_t;

// Fountain decoder structure
struct fountain_decoder {
  size_t processed_parts_count;
  fountain_decoder_result_t *result;

  // Header fields fixed by the first accepted part.
  size_t expected_seq_len;
  size_t expected_fragment_len;
  size_t expected_message_len;
  uint32_t expected_checksum;

  // Recovered fragments, indexed by fragment number: NULL until recovered,
  // then an expected_fragment_len buffer. Allocated with the first part, so
  // it doubles as the "initialised" flag.
  uint8_t **fragments;
  size_t received_count;

  // Work buffer for fragment selection, expected_seq_len entries.
  size_t *choose_scratch;

  // Hash-based mixed parts storage
  mixed_parts_hash_t *mixed_parts_hash;

  // Lightweight duplicate detection (stores only hashes, not full parts)
  hash_set_t received_fragments_hashes;

  // Processing queue
  part_queue_t queue;

  // Cached degree sampler (avoids repeated allocation per fountain fragment)
  random_sampler_t degree_sampler;

  // Equation elimination can reduce the mixed-frame coverage score even as
  // the decoder gains information. Keep the public weighted estimate
  // monotonic, as documented, by remembering its high-water mark.
  float maximum_weighted_progress;

  // Duplicate detection: store last fragment sequence number
  uint32_t last_fragment_seq_num;
  bool has_received_fragment;

  // A recovered fragment was left in mixed_parts_hash because the work queue
  // could not take it; promote_deferred_parts() retries once the queue drains.
  bool has_deferred_parts;
  // A recovered fragment was dropped outright because the queue could not be
  // extended, or the message could not be assembled for lack of memory.
  // Cleared at the start of each receive_part().
  bool alloc_failed;

#ifdef DEBUG_STATS
  // Statistics for resource tracking
  size_t maximum_mixed_parts;
  size_t mixed_from_fragments; // Mixed parts directly from received fragments
  size_t mixed_from_reduction; // Mixed parts created by reduce_mixed_by
  size_t mixed_from_cross_reduction; // Mixed parts created by
                                     // reduce_mixed_against_mixed
  size_t mixed_parts_useful;         // Mixed parts that led to simple parts
#endif
};

// Configuration constants
#ifndef QUEUE_INITIAL_CAPACITY
#define QUEUE_INITIAL_CAPACITY 8
#endif
// Upper bound on the work queue. A single simple pivot can resolve many cached
// mixed equations at once, so a fixed capacity silently discarded recovered
// fragments and could stall an otherwise-sufficient animation. The queue now
// grows on demand, but stays bounded so a hostile stream cannot drive unbounded
// allocation. Matches the decoder's UR_MAX_SEQ_LEN policy cap.
#ifndef QUEUE_MAX_CAPACITY
#define QUEUE_MAX_CAPACITY 1024u
#endif
#define INDEXES_INITIAL_CAPACITY 4
#define HASH_MIN_CAPACITY 64
#define HASH_CAPACITY_MULTIPLIER 1
#define MAX_MIXED_PARTS 256 // Limit mixed parts to prevent memory explosion
#define MAX_DUPLICATE_TRACKING 512 // Limit duplicate tracking set size

#ifdef ENABLE_CROSS_REDUCTION
#define CROSS_REDUCTION_MAX_ITERATIONS 7
#endif
#define FNV1A_OFFSET_BASIS 2166136261u
#define FNV1A_PRIME 16777619u

// Hash table for mixed parts, keyed by the equation's own index set
// (value.indexes), which is kept sorted.
typedef struct hash_entry {
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
static UR_WARN_UNUSED_RESULT bool hash_set_init(hash_set_t *set,
                                                size_t capacity) {
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

  safe_free(set->hashes);
  set->count = 0;
  set->capacity = 0;
}

// Check if hash set contains a hash using binary search (sorted array)
static UR_WARN_UNUSED_RESULT bool hash_set_contains(const hash_set_t *set,
                                                    uint32_t hash) {
  if (!set || !set->hashes || set->count == 0)
    return false;

  size_t lo = 0, hi = set->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (set->hashes[mid] == hash)
      return true;
    if (set->hashes[mid] < hash)
      lo = mid + 1;
    else
      hi = mid;
  }
  return false;
}

// Add hash to set in sorted order (returns false if already exists or on error)
static UR_WARN_UNUSED_RESULT bool hash_set_add(hash_set_t *set, uint32_t hash) {
  if (!set)
    return false;

  // Binary search for insertion point
  size_t lo = 0, hi = set->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (set->hashes[mid] == hash)
      return false; // Already exists
    if (set->hashes[mid] < hash)
      lo = mid + 1;
    else
      hi = mid;
  }

  // Limit growth to prevent unbounded memory usage on embedded devices
  if (set->count >= MAX_DUPLICATE_TRACKING) {
    return false;
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

  // Shift elements to maintain sorted order
  memmove(set->hashes + lo + 1, set->hashes + lo,
          (set->count - lo) * sizeof(uint32_t));
  set->hashes[lo] = hash;
  set->count++;
  return true;
}

// Initialize hash table with dynamic capacity
static UR_WARN_UNUSED_RESULT bool mixed_hash_init(mixed_parts_hash_t *hash,
                                                  size_t capacity) {
  if (!hash || capacity == 0)
    return false;

  // safe_malloc zero-fills, so every bucket starts empty.
  hash->buckets = safe_malloc(capacity * sizeof(hash_entry_t *));
  if (!hash->buckets)
    return false;

  hash->count = 0;
  hash->capacity = capacity;
  return true;
}

static void hash_entry_free(hash_entry_t *entry) {
  decoder_part_free(&entry->value);
  free(entry);
}

// Free hash table
static void mixed_hash_free(mixed_parts_hash_t *hash) {
  if (!hash || !hash->buckets)
    return;

  for (size_t i = 0; i < hash->capacity; i++) {
    hash_entry_t *entry = hash->buckets[i];
    while (entry) {
      hash_entry_t *next = entry->next;
      hash_entry_free(entry);
      entry = next;
    }
  }

  safe_free(hash->buckets);
  hash->count = 0;
}

static hash_entry_t *mixed_hash_find(const mixed_parts_hash_t *hash,
                                     const part_indexes_t *key,
                                     size_t key_hash) {
  for (hash_entry_t *entry = hash->buckets[key_hash % hash->capacity]; entry;
       entry = entry->next) {
    if (entry->key_hash == key_hash &&
        part_indexes_equal(&entry->value.indexes, key))
      return entry;
  }
  return NULL;
}

// Move *part into the table. Fails, leaving *part with the caller, if the
// same equation is already stored or the entry cannot be allocated.
static UR_WARN_UNUSED_RESULT bool mixed_hash_insert(mixed_parts_hash_t *hash,
                                                    decoder_part_t *part) {
  if (!hash || !hash->buckets || !part || hash->capacity == 0)
    return false;

  size_t key_hash = hash_indexes(&part->indexes);
  if (mixed_hash_find(hash, &part->indexes, key_hash))
    return false;

  hash_entry_t *entry = safe_malloc(sizeof(hash_entry_t));
  if (!entry)
    return false;

  entry->value = *part;
  *part = (decoder_part_t){0};
  entry->key_hash = key_hash;

  size_t bucket = key_hash % hash->capacity;
  entry->next = hash->buckets[bucket];
  hash->buckets[bucket] = entry;
  hash->count++;
  return true;
}

#ifdef ENABLE_CROSS_REDUCTION
static UR_WARN_UNUSED_RESULT bool
mixed_hash_contains(const mixed_parts_hash_t *hash, const part_indexes_t *key) {
  if (!hash || !hash->buckets || !key || hash->capacity == 0)
    return false;
  return mixed_hash_find(hash, key, hash_indexes(key)) != NULL;
}

static UR_WARN_UNUSED_RESULT bool
mixed_hash_remove_entry(mixed_parts_hash_t *hash, hash_entry_t *target) {
  if (!hash || !hash->buckets || !target || hash->capacity == 0)
    return false;

  size_t bucket = target->key_hash % hash->capacity;
  hash_entry_t *entry = hash->buckets[bucket];
  hash_entry_t *prev = NULL;

  while (entry) {
    if (entry == target) {
      if (prev)
        prev->next = entry->next;
      else
        hash->buckets[bucket] = entry->next;

      hash_entry_free(entry);
      hash->count--;
      return true;
    }
    prev = entry;
    entry = entry->next;
  }

  return false;
}

// Replace one stored equation with an equivalent, simpler equation while its
// other parent remains in the table. Because old = replacement XOR parent,
// this preserves the equation system's rank without growing the bounded mixed
// table. If the replacement already exists, the victim is redundant and can
// simply be removed. Allocations are completed before the victim is mutated.
static UR_WARN_UNUSED_RESULT bool
mixed_hash_replace_entry(mixed_parts_hash_t *hash, hash_entry_t *victim,
                         const decoder_part_t *replacement,
                         bool *stored_new_equation) {
  if (!hash || !victim || !replacement || !stored_new_equation)
    return false;

  *stored_new_equation = false;

  if (mixed_hash_contains(hash, &replacement->indexes))
    return mixed_hash_remove_entry(hash, victim);

  decoder_part_t new_value = {0};
  if (!decoder_part_copy(replacement, &new_value))
    return false;

  size_t old_bucket = victim->key_hash % hash->capacity;
  hash_entry_t *entry = hash->buckets[old_bucket];
  hash_entry_t *prev = NULL;
  while (entry && entry != victim) {
    prev = entry;
    entry = entry->next;
  }
  if (!entry) {
    decoder_part_free(&new_value);
    return false;
  }

  if (prev)
    prev->next = victim->next;
  else
    hash->buckets[old_bucket] = victim->next;

  decoder_part_free(&victim->value);
  victim->value = new_value;
  victim->key_hash = hash_indexes(&victim->value.indexes);

  size_t new_bucket = victim->key_hash % hash->capacity;
  victim->next = hash->buckets[new_bucket];
  hash->buckets[new_bucket] = victim;
  *stored_new_equation = true;
  return true;
}
#endif

static UR_WARN_UNUSED_RESULT bool queue_init(part_queue_t *queue,
                                             size_t capacity) {
  if (!queue || capacity == 0)
    return false;

  queue->parts = safe_malloc(capacity * sizeof(decoder_part_t));
  if (!queue->parts)
    return false;

  queue->front = 0;
  queue->rear = 0;
  queue->count = 0;
  queue->capacity = capacity;

  return true;
}

static void queue_free(part_queue_t *queue) {
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

static void decoder_part_move(decoder_part_t *src, decoder_part_t *dst) {
  if (!src || !dst)
    return;

  decoder_part_free(dst);
  *dst = *src;
  *src = (decoder_part_t){0};
}

// Ensure room for at least `need` entries, growing the ring buffer if
// necessary. Returns false only when already at QUEUE_MAX_CAPACITY or on OOM.
static UR_WARN_UNUSED_RESULT bool queue_reserve(part_queue_t *queue,
                                                size_t need) {
  if (!queue)
    return false;
  if (need <= queue->capacity)
    return true;
  if (queue->capacity >= QUEUE_MAX_CAPACITY)
    return false;

  size_t new_capacity = queue->capacity ? queue->capacity * 2 : 1;
  while (new_capacity < need)
    new_capacity *= 2;
  if (new_capacity > QUEUE_MAX_CAPACITY)
    new_capacity = QUEUE_MAX_CAPACITY;
  if (need > new_capacity)
    return false;

  // Copy into a fresh buffer in logical order rather than realloc()ing in
  // place. realloc preserves raw indices, which is wrong for a ring buffer
  // whose contents have wrapped: the head segment would be left sitting before
  // the tail instead of after it. Re-seating the elements at front = 0 is
  // correct for every capacity and wrap state, and costs one memcpy of at most
  // QUEUE_MAX_CAPACITY entries a handful of times per message. Entries are
  // plain structs owning heap pointers, so copying the bytes moves ownership.
  decoder_part_t *parts = safe_malloc(new_capacity * sizeof(decoder_part_t));
  if (!parts)
    return false;

  for (size_t i = 0; i < queue->count; i++)
    parts[i] = queue->parts[(queue->front + i) % queue->capacity];

  free(queue->parts);
  queue->parts = parts;
  queue->front = 0;
  queue->rear = queue->count % new_capacity;
  queue->capacity = new_capacity;
  return true;
}

static UR_WARN_UNUSED_RESULT bool queue_enqueue(part_queue_t *queue,
                                                decoder_part_t *part) {
  if (!queue || !part)
    return false;
  if (!queue_reserve(queue, queue->count + 1))
    return false;

  decoder_part_move(part, &queue->parts[queue->rear]);
  queue->rear = (queue->rear + 1) % queue->capacity;
  queue->count++;

  return true;
}

static UR_WARN_UNUSED_RESULT bool queue_dequeue(part_queue_t *queue,
                                                decoder_part_t *part) {
  if (!queue || !part || queue->count == 0)
    return false;

  decoder_part_move(&queue->parts[queue->front], part);
  queue->front = (queue->front + 1) % queue->capacity;
  queue->count--;

  return true;
}

static UR_WARN_UNUSED_RESULT bool queue_is_empty(const part_queue_t *queue) {
  return !queue || queue->count == 0;
}

void decoder_part_free(decoder_part_t *part) {
  if (!part)
    return;

  safe_free(part->indexes.indexes);
  part->indexes.count = 0;
  part->indexes.capacity = 0;

  safe_free(part->data);
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
    dst->data = safe_malloc_uninit(src->data_len);
    if (!dst->data) {
      safe_free(dst->indexes.indexes);
      dst->indexes.count = 0;
      dst->indexes.capacity = 0;
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

  // Binary search for insertion point (keeps array sorted)
  size_t left = 0, right = indexes->count;
  while (left < right) {
    size_t mid = left + (right - left) / 2;
    if (indexes->indexes[mid] == index) {
      return true; // Already exists
    }
    if (indexes->indexes[mid] < index) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  // 'left' is now the insertion point

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

  // Shift elements to make room for new index
  memmove(indexes->indexes + left + 1, indexes->indexes + left,
          (indexes->count - left) * sizeof(size_t));
  indexes->indexes[left] = index;
  indexes->count++;
  return true;
}

bool part_indexes_contains(const part_indexes_t *indexes, size_t index) {
  if (!indexes || indexes->count == 0)
    return false;

  // Binary search on sorted array - O(log n)
  size_t left = 0, right = indexes->count;
  while (left < right) {
    size_t mid = left + (right - left) / 2;
    if (indexes->indexes[mid] == index) {
      return true;
    }
    if (indexes->indexes[mid] < index) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return false;
}

void part_indexes_clear(part_indexes_t *indexes) {
  if (indexes) {
    indexes->count = 0;
  }
}

// Remove every element of `sub` from `set`, in place. Both are sorted; the
// callers only pass a `sub` that is a subset of `set`.
static void part_indexes_subtract(part_indexes_t *set,
                                  const part_indexes_t *sub) {
  size_t kept = 0, j = 0;
  for (size_t i = 0; i < set->count; i++) {
    size_t index = set->indexes[i];
    while (j < sub->count && sub->indexes[j] < index)
      j++;
    if (j < sub->count && sub->indexes[j] == index) {
      j++;
      continue;
    }
    set->indexes[kept++] = index;
  }
  set->count = kept;
}

fountain_decoder_t *fountain_decoder_new(void) {
  fountain_decoder_t *decoder = safe_malloc(sizeof(fountain_decoder_t));
  if (!decoder)
    return NULL;

  if (!queue_init(&decoder->queue, QUEUE_INITIAL_CAPACITY)) {
    free(decoder);
    return NULL;
  }

  return decoder;
}

static void free_fragments(fountain_decoder_t *decoder) {
  if (decoder->fragments) {
    for (size_t i = 0; i < decoder->expected_seq_len; i++)
      free(decoder->fragments[i]);
    safe_free(decoder->fragments);
  }
  decoder->received_count = 0;
}

void fountain_decoder_free(fountain_decoder_t *decoder) {
  if (!decoder)
    return;

  if (decoder->result) {
    free(decoder->result->data);
    free(decoder->result);
  }

  free_fragments(decoder);
  free(decoder->choose_scratch);

  // Free hash table for mixed parts
  if (decoder->mixed_parts_hash) {
    mixed_hash_free(decoder->mixed_parts_hash);
    safe_free(decoder->mixed_parts_hash);
  }

  // Free hash set for duplicate detection
  hash_set_free(&decoder->received_fragments_hashes);

  // Free cached degree sampler
  random_sampler_free(&decoder->degree_sampler);

  queue_free(&decoder->queue);

  free(decoder);
}

static UR_WARN_UNUSED_RESULT bool create_decoder_part_from_encoder_part(
    fountain_decoder_t *const decoder,
    fountain_encoder_part_t *const encoder_part,
    decoder_part_t *const decoder_part) {
  if (!encoder_part || !decoder_part) {
    return false;
  }

  *decoder_part = (decoder_part_t){0};

  if (!choose_fragments_with_scratch(
          encoder_part->seq_num, encoder_part->seq_len, encoder_part->checksum,
          &decoder_part->indexes, &decoder->degree_sampler,
          decoder->choose_scratch)) {
    decoder_part_free(decoder_part);
    return false;
  }

  if (encoder_part->data && encoder_part->data_len > 0) {
    // Move data from encoder_part instead of copying
    decoder_part->data = encoder_part->data;
    decoder_part->data_len = encoder_part->data_len;
    encoder_part->data = NULL;
    encoder_part->data_len = 0;
  }

  return true;
}

typedef enum {
  MIXED_SOURCE_FRAGMENT,
  MIXED_SOURCE_REDUCTION,
  MIXED_SOURCE_CROSS_REDUCTION
} mixed_part_source_t;

static UR_WARN_UNUSED_RESULT bool
is_simple_part(const decoder_part_t *const part) {
  return part && part->indexes.count == 1;
}

static size_t get_part_index(const decoder_part_t *const part) {
  if (!part || part->indexes.count == 0)
    return 0;
  return part->indexes.indexes[0];
}

static UR_WARN_UNUSED_RESULT bool
is_received(const fountain_decoder_t *const decoder, size_t index) {
  return index < decoder->expected_seq_len && decoder->fragments[index];
}

// Take a recovered fragment's buffer into the fragment table. Fails, leaving
// the buffer with `part`, if that fragment is already known.
static UR_WARN_UNUSED_RESULT bool store_fragment(fountain_decoder_t *decoder,
                                                 decoder_part_t *part) {
  size_t index = get_part_index(part);
  if (is_received(decoder, index) || index >= decoder->expected_seq_len ||
      !part->data || part->data_len != decoder->expected_fragment_len)
    return false;

  decoder->fragments[index] = part->data;
  part->data = NULL;
  part->data_len = 0;
  decoder->received_count++;
  return true;
}

static void assemble_message(fountain_decoder_t *decoder) {
  uint8_t *message = safe_malloc_uninit(decoder->expected_message_len);
  fountain_decoder_result_t *result =
      safe_malloc(sizeof(fountain_decoder_result_t));
  if (!message || !result) {
    free(message);
    free(result);
    decoder->alloc_failed = true;
    return;
  }

  // The geometry checked when the first part arrived makes the fragments
  // cover the message exactly, so every byte of the uninitialised buffer is
  // written before it is read.
  size_t offset = 0;
  for (size_t i = 0; i < decoder->expected_seq_len; i++) {
    size_t copy_len = decoder->expected_message_len - offset;
    if (copy_len > decoder->expected_fragment_len)
      copy_len = decoder->expected_fragment_len;
    memcpy(message + offset, decoder->fragments[i], copy_len);
    offset += copy_len;
  }

  if (crc32_calculate(message, decoder->expected_message_len) ==
      decoder->expected_checksum) {
    result->data = message;
    result->data_len = decoder->expected_message_len;
    result->is_success = true;
    result->is_error = false;
  } else {
    free(message);
    result->data = NULL;
    result->data_len = 0;
    result->is_success = false;
    result->is_error = true;
  }
  decoder->result = result;

#ifdef DEBUG_STATS
  printf("=== Mixed Parts Statistics ===\n");
  printf("Maximum mixed parts reached: %zu\n", decoder->maximum_mixed_parts);
  printf("  From fragments: %zu\n", decoder->mixed_from_fragments);
  printf("  From reduction: %zu\n", decoder->mixed_from_reduction);
  printf("  From cross-reduction: %zu\n", decoder->mixed_from_cross_reduction);
  printf("  Total created: %zu\n", decoder->mixed_from_fragments +
                                       decoder->mixed_from_reduction +
                                       decoder->mixed_from_cross_reduction);
  printf("Mixed parts that were useful: %zu\n", decoder->mixed_parts_useful);
#endif
}

// Store a mixed equation, taking ownership of *part on success. Fails, leaving
// *part with the caller, at MAX_MIXED_PARTS, on a duplicate, or on OOM.
static UR_WARN_UNUSED_RESULT bool
add_mixed_part(fountain_decoder_t *const decoder, decoder_part_t *const part,
               const mixed_part_source_t source) {
  if (!decoder || !part || is_simple_part(part) || !decoder->mixed_parts_hash)
    return false;

  // Limit mixed parts to prevent memory explosion on embedded devices
  // When limit is reached, skip adding new mixed parts but continue processing
  if (decoder->mixed_parts_hash->count >= MAX_MIXED_PARTS) {
    return false; // Limit reached, skip adding this mixed part
  }

  // In-place reduction leaves the index array at the capacity of the
  // original, unreduced equation. Stored equations can live for the rest of
  // the scan, so give the slack back (best effort).
  if (part->indexes.capacity > part->indexes.count) {
    size_t *shrunk = safe_realloc(part->indexes.indexes,
                                  part->indexes.count * sizeof(size_t));
    if (shrunk) {
      part->indexes.indexes = shrunk;
      part->indexes.capacity = part->indexes.count;
    }
  }

  if (!mixed_hash_insert(decoder->mixed_parts_hash, part)) {
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
  (void)source;

  return true;
}

static void fountain_decoder_clear_initialization(fountain_decoder_t *decoder) {
  if (!decoder)
    return;

  free_fragments(decoder);
  safe_free(decoder->choose_scratch);
  if (decoder->mixed_parts_hash) {
    mixed_hash_free(decoder->mixed_parts_hash);
    safe_free(decoder->mixed_parts_hash);
  }
  hash_set_free(&decoder->received_fragments_hashes);
  random_sampler_free(&decoder->degree_sampler);

  decoder->expected_seq_len = 0;
  decoder->expected_fragment_len = 0;
  decoder->expected_message_len = 0;
  decoder->expected_checksum = 0;
}

// Eliminate the equation (key, data) from every stored equation that strictly
// contains it, in place. Equations left with a single fragment go to the work
// queue; the rest are re-bucketed under their smaller key.
static void reduce_mixed_by(fountain_decoder_t *const decoder,
                            const part_indexes_t *const key,
                            const uint8_t *const data) {
  if (!decoder || !key || !data || !decoder->mixed_parts_hash ||
      decoder->mixed_parts_hash->count == 0)
    return;

  mixed_parts_hash_t *hash = decoder->mixed_parts_hash;

  // Entries that reduce to a new bucket are unlinked and threaded onto
  // rehash_list via entry->next, then reinserted after the walk. This
  // avoids the per-call malloc for a temp pointer array.
  hash_entry_t *rehash_list = NULL;

  for (size_t i = 0; i < hash->capacity; i++) {
    hash_entry_t *entry = hash->buckets[i];
    hash_entry_t *prev = NULL;

    while (entry) {
      hash_entry_t *next = entry->next;

      if (!part_indexes_is_strict_subset(key, &entry->value.indexes)) {
        prev = entry;
        entry = next;
        continue;
      }

      part_indexes_subtract(&entry->value.indexes, key);
      size_t xor_len = entry->value.data_len < decoder->expected_fragment_len
                           ? entry->value.data_len
                           : decoder->expected_fragment_len;
      ur_xor_inplace(entry->value.data, data, xor_len);
      entry->key_hash = hash_indexes(&entry->value.indexes);

      if (is_simple_part(&entry->value)) {
#ifdef DEBUG_STATS
        decoder->mixed_parts_useful++;
#endif
        size_t fragment_idx = get_part_index(&entry->value);
        if (!is_received(decoder, fragment_idx) &&
            !queue_enqueue(&decoder->queue, &entry->value)) {
          // The queue grows on demand, so this only fails at
          // QUEUE_MAX_CAPACITY or on OOM. Freeing here would discard a
          // fragment already reconstructed from equations that have since
          // been consumed - the sender may never send another frame carrying
          // it. Leave the equation where it is instead: retaining costs no
          // allocation, which is what makes it viable on the OOM path.
          // promote_deferred_parts() collects it once the queue has drained.
          decoder->has_deferred_parts = true;
          prev = entry;
          entry = next;
          continue;
        }

        // Unlink from this bucket and free.
        if (prev) {
          prev->next = next;
        } else {
          hash->buckets[i] = next;
        }
        hash_entry_free(entry);
        hash->count--;
        entry = next;
        continue;
      }

      size_t new_bucket = entry->key_hash % hash->capacity;
      if (new_bucket != i) {
        // Unlink and stash for reinsertion below.
        if (prev) {
          prev->next = next;
        } else {
          hash->buckets[i] = next;
        }
        entry->next = rehash_list;
        rehash_list = entry;
        entry = next;
        continue;
      }

      prev = entry;
      entry = next;
    }
  }

  // Reinsert stashed entries at the head of their new bucket. A revisit
  // during this call isn't possible — the outer bucket walk has already
  // finished by the time we get here.
  while (rehash_list) {
    hash_entry_t *e = rehash_list;
    rehash_list = rehash_list->next;
    size_t b = e->key_hash % hash->capacity;
    e->next = hash->buckets[b];
    hash->buckets[b] = e;
  }

#ifdef DEBUG_STATS
  if (hash->count > decoder->maximum_mixed_parts) {
    decoder->maximum_mixed_parts = hash->count;
  }
#endif
}

// Collect equations that reduced to a single fragment but could not be handed
// to the work queue at the time (see reduce_mixed_by()). Returns true if at
// least one entry left the table, so the caller can drain and try again.
// Allocation-free apart from the enqueue itself, and each pass strictly
// shrinks the table, so the retry loop terminates.
static UR_WARN_UNUSED_RESULT bool
promote_deferred_parts(fountain_decoder_t *const decoder) {
  if (!decoder || !decoder->has_deferred_parts || !decoder->mixed_parts_hash)
    return false;

  mixed_parts_hash_t *hash = decoder->mixed_parts_hash;
  bool promoted = false;
  bool still_deferred = false;

  for (size_t i = 0; i < hash->capacity; i++) {
    hash_entry_t *entry = hash->buckets[i];
    hash_entry_t *prev = NULL;

    while (entry) {
      hash_entry_t *next = entry->next;

      if (!is_simple_part(&entry->value)) {
        prev = entry;
        entry = next;
        continue;
      }

      // Already-received fragments fall through to the unlink below: dropping
      // a duplicate is not a loss.
      size_t fragment_idx = get_part_index(&entry->value);
      if (!is_received(decoder, fragment_idx) &&
          !queue_enqueue(&decoder->queue, &entry->value)) {
        still_deferred = true;
        prev = entry;
        entry = next;
        continue;
      }

      if (prev) {
        prev->next = next;
      } else {
        hash->buckets[i] = next;
      }
      hash_entry_free(entry);
      hash->count--;
      promoted = true;
      entry = next;
    }
  }

  decoder->has_deferred_parts = still_deferred;
  return promoted;
}

#ifdef ENABLE_CROSS_REDUCTION
static UR_WARN_UNUSED_RESULT bool
reduce_part_by_part(const decoder_part_t *const a,
                    const decoder_part_t *const b,
                    decoder_part_t *const result) {
  if (!a || !b || !result)
    return false;

  // Only reduce if b's indexes are a strict subset of a's indexes
  if (!part_indexes_is_strict_subset(&b->indexes, &a->indexes)) {
    return decoder_part_copy(a, result);
  }

  *result = (decoder_part_t){0};

  if (!part_indexes_difference(&a->indexes, &b->indexes, &result->indexes)) {
    safe_free(result->indexes.indexes);
    return false;
  }

  result->data = safe_malloc_uninit(a->data_len);
  if (!result->data) {
    safe_free(result->indexes.indexes);
    return false;
  }

  result->data_len = a->data_len;
  ur_xor(result->data, a->data, b->data, a->data_len);
  return true;
}

static UR_WARN_UNUSED_RESULT bool
create_symmetric_diff(const decoder_part_t *const a,
                      const decoder_part_t *const b,
                      decoder_part_t *const result) {
  if (!a || !b || !result || a->data_len != b->data_len)
    return false;

  *result = (decoder_part_t){0};

  if (!part_indexes_symmetric_difference(&a->indexes, &b->indexes,
                                         &result->indexes)) {
    safe_free(result->indexes.indexes);
    return false;
  }

  if (result->indexes.count == 0) {
    safe_free(result->indexes.indexes);
    return false;
  }

  result->data = safe_malloc_uninit(a->data_len);
  if (!result->data) {
    safe_free(result->indexes.indexes);
    result->indexes.count = 0;
    result->indexes.capacity = 0;
    return false;
  }

  result->data_len = a->data_len;
  ur_xor(result->data, a->data, b->data, a->data_len);

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

        if (part_indexes_have_intersection(&entries[i]->value.indexes,
                                           &entries[j]->value.indexes)) {

          decoder_part_t new_part = {0};

          if (create_symmetric_diff(&entries[i]->value, &entries[j]->value,
                                    &new_part)) {

            // Replacing a parent with its XOR against the other parent is a
            // reversible row operation. Prefer the larger eligible parent so
            // the table becomes strictly simpler without accumulating extra
            // equations and crowding out future fountain parts.
            hash_entry_t *victim = NULL;
            if (new_part.indexes.count < entries[i]->value.indexes.count)
              victim = entries[i];
            if (new_part.indexes.count < entries[j]->value.indexes.count &&
                (!victim ||
                 entries[j]->value.indexes.count > victim->value.indexes.count))
              victim = entries[j];

            if (!victim) {
              decoder_part_free(&new_part);
              continue;
            }

            if (new_part.indexes.count > 0) {
              if (is_simple_part(&new_part)) {
                size_t fragment_idx = get_part_index(&new_part);
                if (!is_received(decoder, fragment_idx)) {
                  // Queue the recovered fragment before removing its parent:
                  // an allocation failure must leave the equation system
                  // intact so a later fountain part can retry the reduction.
                  if (queue_enqueue(&decoder->queue, &new_part) &&
                      mixed_hash_remove_entry(decoder->mixed_parts_hash,
                                              victim)) {
#ifdef DEBUG_STATS
                    decoder->mixed_parts_useful++;
#endif
                    made_progress = true;
                  } else {
                    decoder->alloc_failed = true;
                  }
                }
                decoder_part_free(&new_part);
                break;
              } else {
                bool stored_new_equation = false;
                if (mixed_hash_replace_entry(decoder->mixed_parts_hash, victim,
                                             &new_part, &stored_new_equation)) {
#ifdef DEBUG_STATS
                  if (stored_new_equation)
                    decoder->mixed_from_cross_reduction++;
#else
                  (void)stored_new_equation;
#endif
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
      if (part_indexes_equal(&entry->value.indexes, &pivot->indexes)) {
        prev = entry;
        entry = next;
        continue;
      }

      if (part_indexes_is_strict_subset(&pivot->indexes,
                                        &entry->value.indexes)) {
        decoder_part_t reduced = {0};

        if (reduce_part_by_part(&entry->value, pivot, &reduced)) {
          // Unlink entry from bucket chain BEFORE freeing
          if (prev) {
            prev->next = next;
          } else {
            decoder->mixed_parts_hash->buckets[b] = next;
          }

          hash_entry_free(entry);
          decoder->mixed_parts_hash->count--;

          // Handle reduced part
          if (is_simple_part(&reduced)) {
            size_t fragment_idx = get_part_index(&reduced);
            if (!is_received(decoder, fragment_idx)) {
              // Unlike reduce_mixed_by() there is nowhere to retain this -
              // the entry it came from has already been unlinked and freed -
              // so report the loss rather than returning success on a decode
              // that silently dropped data.
              if (!queue_enqueue(&decoder->queue, &reduced))
                decoder->alloc_failed = true;
            }
          } else {
            // Replacing an equation is lossless if the reduced equation is
            // already present. Any other insertion failure means the original
            // equation was removed without a replacement, so surface it to
            // the caller as the same transient allocation failure used for a
            // dropped recovered fragment.
            if (add_mixed_part(decoder, &reduced, MIXED_SOURCE_REDUCTION)) {
              // The insertion goes to the head of the reduced equation's
              // bucket, which may be the bucket this walk is in the middle
              // of. With prev still NULL the next unlink below would assign
              // buckets[b] and orphan the equation just stored - leaking it
              // and leaving count above the number of reachable entries,
              // which reduce_mixed_against_mixed() then reads as NULL slots.
              // Re-anchor prev on the new head when that happened.
              if (!prev && decoder->mixed_parts_hash->buckets[b] != next) {
                prev = decoder->mixed_parts_hash->buckets[b];
              }
            } else if (!mixed_hash_contains(decoder->mixed_parts_hash,
                                            &reduced.indexes)) {
              decoder->alloc_failed = true;
            }
          }
          decoder_part_free(&reduced);

          // prev still points at the previous valid entry (or at the new head
          // re-anchored above; NULL if we removed the head and added nothing)
          entry = next;
          continue;
        }
      }

      prev = entry;
      entry = next;
    }
  }
}
#endif // ENABLE_CROSS_REDUCTION

// Reduces *part in place, then queues or stores it, taking its buffers.
static void process_mixed_part(fountain_decoder_t *const decoder,
                               decoder_part_t *const part) {
  if (!decoder || !part || is_simple_part(part) || !decoder->mixed_parts_hash)
    return;

  part_indexes_t *indexes = &part->indexes;
  size_t len = decoder->expected_fragment_len;
  if (!part->data || part->data_len != len)
    return;

  // Eliminate recovered fragments. Like reducing by each simple part in turn,
  // which needs a strict subset, this stops with one index left.
  size_t remaining = indexes->count, kept = 0;
  for (size_t i = 0; i < indexes->count; i++) {
    size_t index = indexes->indexes[i];
    if (remaining > 1 && is_received(decoder, index)) {
      ur_xor_inplace(part->data, decoder->fragments[index], len);
      remaining--;
      continue;
    }
    indexes->indexes[kept++] = index;
  }
  indexes->count = kept;

  mixed_parts_hash_t *hash = decoder->mixed_parts_hash;
  for (size_t i = 0; i < hash->capacity && !is_simple_part(part); i++) {
    for (hash_entry_t *entry = hash->buckets[i]; entry; entry = entry->next) {
      if (part_indexes_is_strict_subset(&entry->value.indexes, indexes)) {
        part_indexes_subtract(indexes, &entry->value.indexes);
        ur_xor_inplace(part->data, entry->value.data, len);
      }
    }
  }

  if (is_simple_part(part)) {
    // Locally derived from the incoming fragment, so there is no table entry
    // to fall back on - report the loss. See reduce_mixed_by().
    if (!queue_enqueue(&decoder->queue, part))
      decoder->alloc_failed = true;
    return;
  }

  reduce_mixed_by(decoder, indexes, part->data);
  if (!add_mixed_part(decoder, part, MIXED_SOURCE_FRAGMENT)) {
    // The table is at MAX_MIXED_PARTS, the equation is a duplicate, or the
    // allocation failed. Dropping it is the documented behaviour of the cap:
    // the fountain stream keeps supplying parts, so the message still
    // converges, just from more frames.
#ifdef ENABLE_CROSS_REDUCTION
  } else {
    // A newly stored mixed equation may combine with equations already in
    // the table even when neither is a subset of the other. The ordinary
    // reduce_mixed_by() path cannot discover those relationships, so run
    // the bounded mixed-against-mixed pass while the new information is
    // available. Parts recovered here are queued and consumed by the outer
    // receive loop.
    reduce_mixed_against_mixed(decoder);
#endif
  }
}

static void process_queue_item(fountain_decoder_t *const decoder) {
  if (!decoder || queue_is_empty(&decoder->queue))
    return;

  decoder_part_t part = {0};

  if (!queue_dequeue(&decoder->queue, &part))
    return;

  if (is_simple_part(&part)) {
    size_t index = get_part_index(&part);
    part_indexes_t key = {.indexes = &index, .count = 1, .capacity = 1};
    const uint8_t *data = part.data;

    if (store_fragment(decoder, &part)) {
      data = decoder->fragments[index];
      if (decoder->received_count == decoder->expected_seq_len)
        assemble_message(decoder);
    }
    reduce_mixed_by(decoder, &key, data);
  } else {
    process_mixed_part(decoder, &part);
  }

  decoder_part_free(&part);
}

static UR_WARN_UNUSED_RESULT bool
fountain_decoder_initialize(fountain_decoder_t *decoder,
                            const fountain_encoder_part_t *part) {
  // Every fragment is ceil(message_len / seq_len) bytes, the last one
  // zero-padded, so the fragments cover the message exactly.
  if (part->seq_len == 0 || part->message_len == 0 ||
      part->data_len != part->message_len / part->seq_len +
                            (part->message_len % part->seq_len ? 1 : 0))
    return false;

  decoder->fragments = safe_malloc(part->seq_len * sizeof(uint8_t *));
  decoder->choose_scratch = safe_malloc_uninit(part->seq_len * sizeof(size_t));
  if (!decoder->fragments || !decoder->choose_scratch) {
    fountain_decoder_clear_initialization(decoder);
    return false;
  }

  // A single simple pivot can resolve many cached mixed equations at once,
  // so size the work queue up front rather than growing it a step at a time.
  // Bounded by MAX_MIXED_PARTS, not QUEUE_MAX_CAPACITY: what the queue has to
  // absorb in one burst is the equations held in the mixed table, and that is
  // where the cap actually bites. Best-effort - queue_enqueue() still grows on
  // demand if a message genuinely needs more.
  if (!queue_reserve(&decoder->queue, part->seq_len < MAX_MIXED_PARTS
                                          ? part->seq_len
                                          : MAX_MIXED_PARTS)) {
    // Best effort - queue_enqueue() still grows on demand.
  }

  decoder->expected_seq_len = part->seq_len;
  decoder->expected_checksum = part->checksum;
  decoder->expected_fragment_len = part->data_len;
  decoder->expected_message_len = part->message_len;

  size_t hash_capacity =
      part->seq_len < (HASH_MIN_CAPACITY / HASH_CAPACITY_MULTIPLIER)
          ? HASH_MIN_CAPACITY
          : part->seq_len * HASH_CAPACITY_MULTIPLIER;

  decoder->mixed_parts_hash = safe_malloc(sizeof(mixed_parts_hash_t));
  if (!decoder->mixed_parts_hash ||
      !mixed_hash_init(decoder->mixed_parts_hash, hash_capacity) ||
      !hash_set_init(&decoder->received_fragments_hashes,
                     hash_capacity < MAX_DUPLICATE_TRACKING
                         ? hash_capacity
                         : MAX_DUPLICATE_TRACKING)) {
    fountain_decoder_clear_initialization(decoder);
    return false;
  }

  // Degree probs and the sampler stay double — interop-critical, must
  // match reference implementations bit-for-bit (see fountain_utils.c).
  double *degree_probs = safe_malloc(part->seq_len * sizeof(double));
  if (!degree_probs) {
    fountain_decoder_clear_initialization(decoder);
    return false;
  }
  for (size_t i = 0; i < part->seq_len; i++) {
    degree_probs[i] = 1.0 / (i + 1);
  }
  bool sampler_ok = random_sampler_init(&decoder->degree_sampler, degree_probs,
                                        part->seq_len);
  free(degree_probs);
  if (!sampler_ok) {
    fountain_decoder_clear_initialization(decoder);
    return false;
  }
  return true;
}

bool fountain_decoder_receive_part(fountain_decoder_t *decoder,
                                   fountain_encoder_part_t *part) {
  if (!decoder || !part) {
    return false;
  }

  if (fountain_decoder_is_complete(decoder)) {
    return false;
  }

  // Reassembly failed for lack of memory when the last fragment arrived.
  // Every fragment is held, so no later part would trigger it again.
  if (decoder->fragments &&
      decoder->received_count == decoder->expected_seq_len) {
    decoder->alloc_failed = false;
    assemble_message(decoder);
    return !decoder->alloc_failed;
  }

  if (decoder->has_received_fragment &&
      part->seq_num == decoder->last_fragment_seq_num) {
    return true;
  }

  // seq_num 0 would select fragment index (uint32_t)-1.
  if (part->seq_num == 0) {
    return false;
  }

  if (!decoder->fragments && !fountain_decoder_initialize(decoder, part)) {
    return false;
  }

  // Every part of a message must agree on all four header fields. The first
  // part establishes them above (so it always passes), and each later part is
  // checked against them here.
  //
  // Fragment length matters for memory safety: accepting a shorter part would
  // let the XOR reduction read past its buffer. The other three matter for
  // integrity: a later part's own seq_len and checksum drive fragment
  // selection, so a frame from a different message - an interleaved animation
  // of the same UR type, or a crafted one - would otherwise be mixed into this
  // message's equations, corrupting or permanently stranding reassembly.
  if (part->data_len != decoder->expected_fragment_len ||
      part->seq_len != decoder->expected_seq_len ||
      part->message_len != decoder->expected_message_len ||
      part->checksum != decoder->expected_checksum) {
    return false;
  }

  decoder->alloc_failed = false;

  decoder_part_t decoder_part;
  if (!create_decoder_part_from_encoder_part(decoder, part, &decoder_part)) {
    return false;
  }

  uint32_t fragment_hash = (uint32_t)hash_indexes(&decoder_part.indexes);
  if (hash_set_contains(&decoder->received_fragments_hashes, fragment_hash)) {
    decoder_part_free(&decoder_part);
    return true;
  }

  if (!hash_set_add(&decoder->received_fragments_hashes, fragment_hash)) {
    // Best effort. Losing an entry only costs the cheap duplicate short-circuit
    // above; a fragment that slips through is filtered again by the fragment
    // table before it can be applied twice.
  }

  if (!queue_enqueue(&decoder->queue, &decoder_part)) {
    decoder_part_free(&decoder_part);
    return false;
  }

  while (!fountain_decoder_is_complete(decoder) &&
         !queue_is_empty(&decoder->queue)) {
    process_queue_item(decoder);
  }

  // Fragments that could not be queued mid-reduction were retained in the
  // equation table rather than dropped. The queue has drained now, so hand
  // them over and drain again; anything still not fitting stays retained for
  // the next frame.
  while (decoder->has_deferred_parts &&
         !fountain_decoder_is_complete(decoder) &&
         promote_deferred_parts(decoder)) {
    while (!fountain_decoder_is_complete(decoder) &&
           !queue_is_empty(&decoder->queue)) {
      process_queue_item(decoder);
    }
  }

  decoder->processed_parts_count++;
  decoder->last_fragment_seq_num = part->seq_num;
  decoder->has_received_fragment = true;

  decoder_part_free(&decoder_part);

  // A recovered fragment was dropped because the queue could not be extended,
  // or the message could not be assembled. Report it rather than returning
  // success - the caller maps this to a non-terminal memory error, so scanning
  // continues and the next part retries or resupplies what was lost.
  return !decoder->alloc_failed;
}

bool fountain_decoder_had_alloc_failure(const fountain_decoder_t *decoder) {
  return decoder && decoder->alloc_failed;
}

bool fountain_decoder_is_complete(fountain_decoder_t *decoder) {
  return decoder && decoder->result != NULL;
}

bool fountain_decoder_is_success(fountain_decoder_t *decoder) {
  return decoder && decoder->result && decoder->result->is_success;
}

size_t fountain_decoder_expected_part_count(fountain_decoder_t *decoder) {
  if (!decoder || !decoder->fragments)
    return 0;
  return decoder->expected_seq_len;
}

float fountain_decoder_estimated_percent_complete(fountain_decoder_t *decoder) {
  if (!decoder)
    return 0.0f;
  if (fountain_decoder_is_complete(decoder))
    return 1.0f;
  if (!decoder->fragments)
    return 0.0f;

  float estimated_input_parts =
      (float)fountain_decoder_expected_part_count(decoder) * 1.75f;
  float progress =
      (float)decoder->processed_parts_count / estimated_input_parts;
  return progress > 0.99f ? 0.99f : progress;
}

// Weighted-mixed-frames completion estimate. Ports SeedSigner's
// weight_mixed_frames=True method (helpers/ur2/fountain_decoder.py): count the
// fully decoded fragments, plus partial credit for fragments that are still
// only present inside mixed (XOR'd) frames. Each mixed frame contributes
// 1/(frames mixed) to every index it covers; each index's total contribution
// is capped at 0.75 so a not-yet-decoded fragment never counts as much as a
// decoded one. The stored high-water mark keeps the estimate monotonic when
// equation elimination changes the mixed-frame coverage.
// Backward-compatible addition — the reference estimate above is unchanged.
float fountain_decoder_estimated_percent_complete_weighted(
    fountain_decoder_t *decoder) {
  if (!decoder)
    return 0.0f;
  if (fountain_decoder_is_complete(decoder))
    return 1.0f;
  size_t parts = fountain_decoder_expected_part_count(decoder);
  if (parts == 0)
    return 0.0f;

  float mixed_score = 0.0f;
  mixed_parts_hash_t *hash = decoder->mixed_parts_hash;
  if (hash && hash->count > 0) {
    // Per-index partial scores from the mixed parts. Fragment indexes are in
    // [0, seq_len) == [0, parts), so a scratch array keyed by index mirrors
    // the Python dict `mixed_index_scoring`. If the scratch allocation fails,
    // mixed_score stays 0 — still the weighted metric, just without partial
    // credit, so the value never jumps to a different scale.
    float *scoring = safe_malloc(parts * sizeof(float));
    if (scoring) {
      for (size_t b = 0; b < hash->capacity; b++) {
        for (hash_entry_t *entry = hash->buckets[b]; entry;
             entry = entry->next) {
          float score = 1.0f / (float)entry->value.indexes.count;
          for (size_t k = 0; k < entry->value.indexes.count; k++) {
            size_t index = entry->value.indexes.indexes[k];
            if (index < parts)
              scoring[index] += score;
          }
        }
      }
      for (size_t i = 0; i < parts; i++) {
        mixed_score += scoring[i] < 0.75f ? scoring[i] : 0.75f;
      }
      safe_free(scoring);
    }
  }

  float num_complete = (float)decoder->received_count;
  float progress = (num_complete + mixed_score) / (float)parts;
  // Never report >= 1.0 while incomplete (same 0.99 cap as the reference
  // estimate): keeps rounded displays below 100% and bounds the result even
  // if a stale mixed entry or mismatched stream breaks the invariant that
  // mixed keys exclude received indexes.
  if (progress > 0.99f)
    progress = 0.99f;
  if (progress > decoder->maximum_weighted_progress)
    decoder->maximum_weighted_progress = progress;
  return decoder->maximum_weighted_progress;
}

uint8_t *fountain_decoder_result_message(fountain_decoder_t *decoder) {
  if (!decoder || !decoder->result)
    return NULL;
  return decoder->result->data;
}

uint8_t *fountain_decoder_take_result_message(fountain_decoder_t *decoder) {
  if (!decoder || !decoder->result)
    return NULL;
  uint8_t *data = decoder->result->data;
  decoder->result->data = NULL;
  return data;
}

size_t fountain_decoder_result_message_len(fountain_decoder_t *decoder) {
  if (!decoder || !decoder->result)
    return 0;
  return decoder->result->data_len;
}

size_t fountain_decoder_processed_parts_count(fountain_decoder_t *decoder) {
  if (!decoder)
    return 0;
  return decoder->processed_parts_count;
}

size_t fountain_decoder_received_parts_count(fountain_decoder_t *decoder) {
  if (!decoder)
    return 0;
  return decoder->received_count;
}
