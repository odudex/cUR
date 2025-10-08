#ifndef FOUNTAIN_DECODER_H
#define FOUNTAIN_DECODER_H

// #define DEBUG_STATS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
typedef struct fountain_decoder fountain_decoder_t;
typedef struct mixed_parts_hash mixed_parts_hash_t;

// Part indexes set (simplified as dynamic array for C)
typedef struct {
  size_t *indexes;
  size_t count;
  size_t capacity;
} part_indexes_t;

// Fountain encoder part structure
typedef struct {
  uint32_t seq_num;
  size_t seq_len;
  size_t message_len;
  uint32_t checksum;
  uint8_t *data;
  size_t data_len;
} fountain_encoder_part_t;

// Internal decoder part structure
typedef struct {
  part_indexes_t indexes;
  uint8_t *data;
  size_t data_len;
} decoder_part_t;

// Queue for processing parts
typedef struct {
  decoder_part_t *parts;
  size_t front;
  size_t rear;
  size_t count;
  size_t capacity;
} part_queue_t;

// Fountain decoder result
typedef struct {
  uint8_t *data;
  size_t data_len;
  bool is_success;
  bool is_error;
} fountain_decoder_result_t;

// Fountain decoder structure
typedef struct fountain_decoder {
  part_indexes_t received_part_indexes;
  part_indexes_t *last_part_indexes;
  size_t processed_parts_count;
  fountain_decoder_result_t *result;
  part_indexes_t *expected_part_indexes;
  size_t expected_fragment_len;
  size_t expected_message_len;
  uint32_t expected_checksum;

  // Simple parts storage (key: single index, value: data)
  struct {
    size_t *keys;
    decoder_part_t *values;
    size_t *value_lens;
    size_t count;
    size_t capacity;
  } simple_parts;

  // Hash-based mixed parts storage
  mixed_parts_hash_t *mixed_parts_hash;

  // Processing queue
  part_queue_t queue;

  // Duplicate detection: store last fragment sequence number
  uint32_t last_fragment_seq_num;
  bool has_received_fragment;

#ifdef DEBUG_STATS
  // Statistics for resource tracking
  size_t maximum_mixed_parts;
  size_t mixed_from_fragments; // Mixed parts directly from received fragments
  size_t mixed_from_reduction; // Mixed parts created by reduce_mixed_by
  size_t mixed_from_cross_reduction; // Mixed parts created by
                                     // reduce_mixed_against_mixed
  size_t mixed_parts_useful;         // Mixed parts that led to simple parts
#endif
} fountain_decoder_t;

// Function declarations

/**
 * Create a new fountain decoder
 * @return Pointer to fountain decoder or NULL on error
 */
fountain_decoder_t *fountain_decoder_new(void);

/**
 * Free fountain decoder
 * @param decoder Pointer to fountain decoder
 */
void fountain_decoder_free(fountain_decoder_t *decoder);

/**
 * Receive a fountain encoder part
 * @param decoder Pointer to fountain decoder
 * @param part Pointer to encoder part
 * @return true on success, false on error
 */
bool fountain_decoder_receive_part(fountain_decoder_t *decoder,
                                   fountain_encoder_part_t *part);

/**
 * Check if decoding is complete
 * @param decoder Pointer to fountain decoder
 * @return true if complete, false otherwise
 */
bool fountain_decoder_is_complete(fountain_decoder_t *decoder);

/**
 * Check if decoding was successful
 * @param decoder Pointer to fountain decoder
 * @return true if successful, false otherwise
 */
bool fountain_decoder_is_success(fountain_decoder_t *decoder);

/**
 * Check if decoding failed
 * @param decoder Pointer to fountain decoder
 * @return true if failed, false otherwise
 */
bool fountain_decoder_is_failure(fountain_decoder_t *decoder);

/**
 * Get expected part count
 * @param decoder Pointer to fountain decoder
 * @return Expected part count
 */
size_t fountain_decoder_expected_part_count(fountain_decoder_t *decoder);

/**
 * Get estimated completion percentage
 * @param decoder Pointer to fountain decoder
 * @return Completion percentage (0.0 to 1.0)
 */
double fountain_decoder_estimated_percent_complete(fountain_decoder_t *decoder);

/**
 * Get result message
 * @param decoder Pointer to fountain decoder
 * @return Pointer to result data or NULL
 */
uint8_t *fountain_decoder_result_message(fountain_decoder_t *decoder);

/**
 * Get result message length
 * @param decoder Pointer to fountain decoder
 * @return Result data length
 */
size_t fountain_decoder_result_message_len(fountain_decoder_t *decoder);

// Helper functions for part indexes
part_indexes_t *part_indexes_new(void);
void part_indexes_free(part_indexes_t *indexes);
bool part_indexes_add(part_indexes_t *indexes, size_t index);
bool part_indexes_contains(const part_indexes_t *indexes, size_t index);
void part_indexes_clear(part_indexes_t *indexes);

// Queue operations
bool queue_init(part_queue_t *queue, size_t capacity);
void queue_free(part_queue_t *queue);
bool queue_enqueue(part_queue_t *queue, const decoder_part_t *part);
bool queue_dequeue(part_queue_t *queue, decoder_part_t *part);
bool queue_is_empty(const part_queue_t *queue);

// Part operations
void decoder_part_free(decoder_part_t *part);
bool decoder_part_copy(const decoder_part_t *src, decoder_part_t *dst);

#endif // FOUNTAIN_DECODER_H