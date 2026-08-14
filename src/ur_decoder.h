#ifndef UR_DECODER_H
#define UR_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ur_attributes.h"

/* Hard caps on attacker-controlled fragment header fields. The first
 * fragment's seq_len drives O(seq_len) allocations inside the fountain
 * decoder (degree sampler, hash table, part-index bitmap); message_len drives
 * the final reassembly buffer. Reject anything larger than what a real Bitcoin
 * UR would ever need, to keep a malicious QR from exhausting embedded heap.
 * Both can be overridden at compile time by integrators with different limits.
 *
 * Exposed here, rather than kept private to ur_decoder.c, so that an encoder
 * can refuse to produce a stream this decoder would reject - a message over
 * these limits yields UR_DECODER_ERROR_UNSUPPORTED_SIZE. */
#ifndef UR_MAX_SEQ_LEN
#define UR_MAX_SEQ_LEN 1024u
#endif
#ifndef UR_MAX_MESSAGE_LEN
#define UR_MAX_MESSAGE_LEN (256u * 1024u)
#endif

/**
 * Decoder state machine. ur_decoder_receive_part() returns the state after
 * processing a part; ur_decoder_get_state() polls it without feeding.
 *
 * Terminal states (UR_DECODER_OK, UR_DECODER_NO_RESULT,
 * UR_DECODER_ERROR_INVALID_CHECKSUM, UR_DECODER_ERROR_UNSUPPORTED_SIZE) are
 * permanent: once reached, every
 * subsequent receive_part() call returns the terminal state without
 * processing the part. All other error states are transient: the offending
 * part was rejected, the state sticks until the next receive_part() call
 * (which resets to UR_DECODER_PROCESSING on entry), and the caller may keep
 * feeding parts — stray or misread QR frames are expected during scanning.
 * Duplicate frames are accepted and stay in UR_DECODER_PROCESSING.
 *
 * ur_decoder_get_result() returns non-NULL if and only if the state is
 * UR_DECODER_OK.
 */
typedef enum {
  UR_DECODER_OK = 0,     /* terminal: finished, get_result() non-NULL */
  UR_DECODER_PROCESSING, /* keep feeding parts */
  UR_DECODER_NO_RESULT,  /* terminal: finished without error, no content */

  /* Errors. All values >= UR_DECODER_ERROR_INVALID_SCHEME are errors;
   * anchored at 16 so future non-error states can be added above without
   * renumbering. Append new errors at the end only, and mirror every new
   * state in the DECODER_* constant tables of both bindings (uUR.c and
   * python/uUR.c). */
  UR_DECODER_ERROR_INVALID_SCHEME = 16,
  UR_DECODER_ERROR_INVALID_TYPE,
  UR_DECODER_ERROR_INVALID_PATH_LENGTH,
  UR_DECODER_ERROR_INVALID_SEQUENCE_COMPONENT,
  UR_DECODER_ERROR_INVALID_FRAGMENT,
  UR_DECODER_ERROR_INVALID_PART,
  UR_DECODER_ERROR_INVALID_CHECKSUM, /* terminal */
  UR_DECODER_ERROR_MEMORY,
  UR_DECODER_ERROR_NULL_POINTER,
  /* terminal: the message is well-formed but declares a sequence length or
   * total size beyond this build's configured limits. Unlike a misread frame
   * this can never succeed, however long the caller keeps scanning, so it is
   * terminal and callers should report it rather than continue. */
  UR_DECODER_ERROR_UNSUPPORTED_SIZE
} ur_decoder_state_t;

static inline bool ur_decoder_state_is_error(ur_decoder_state_t s) {
  return s >= UR_DECODER_ERROR_INVALID_SCHEME;
}

static inline bool ur_decoder_state_is_terminal(ur_decoder_state_t s) {
  return s == UR_DECODER_OK || s == UR_DECODER_NO_RESULT ||
         s == UR_DECODER_ERROR_INVALID_CHECKSUM ||
         s == UR_DECODER_ERROR_UNSUPPORTED_SIZE;
}

typedef struct ur_decoder ur_decoder_t;
typedef struct fountain_decoder fountain_decoder_t;

typedef struct {
  char *type;
  uint8_t *cbor_data;
  size_t cbor_len;
} ur_result_t;

typedef struct ur_decoder {
  fountain_decoder_t *fountain_decoder;
  char *expected_type;
  ur_result_t *result;
  ur_decoder_state_t state;
} ur_decoder_t;

/**
 * Create a new URDecoder instance
 * @return Pointer to URDecoder instance or NULL on error
 */
UR_WARN_UNUSED_RESULT ur_decoder_t *ur_decoder_new(void);

/**
 * Free URDecoder instance
 * @param decoder Pointer to URDecoder instance
 */
void ur_decoder_free(ur_decoder_t *decoder);

/**
 * Receive and process a UR part
 * @param decoder Pointer to URDecoder instance
 * @param part_str UR part string to process
 * @return Decoder state after processing (see ur_decoder_state_t). Returns
 *         UR_DECODER_ERROR_NULL_POINTER on a NULL decoder; on a terminal
 *         decoder returns the terminal state without processing the part.
 */
UR_WARN_UNUSED_RESULT ur_decoder_state_t
ur_decoder_receive_part(ur_decoder_t *decoder, const char *part_str);

/**
 * Get the current decoder state without feeding a part
 * @param decoder Pointer to URDecoder instance
 * @return Current state; UR_DECODER_ERROR_NULL_POINTER if decoder is NULL
 */
ur_decoder_state_t ur_decoder_get_state(const ur_decoder_t *decoder);

/**
 * Get the decoded result
 * @param decoder Pointer to URDecoder instance
 * @return Non-NULL if and only if ur_decoder_get_state() == UR_DECODER_OK
 */
UR_WARN_UNUSED_RESULT ur_result_t *ur_decoder_get_result(ur_decoder_t *decoder);

/**
 * Get expected part count
 * @param decoder Pointer to URDecoder instance
 * @return Expected part count
 */
size_t ur_decoder_expected_part_count(ur_decoder_t *decoder);

/**
 * Get processed parts count
 * @param decoder Pointer to URDecoder instance
 * @return Processed parts count
 */
size_t ur_decoder_processed_parts_count(ur_decoder_t *decoder);

/**
 * Get the number of unique pure fragments recovered so far (out of
 * expected_part_count) — suitable for an "n of m" progress display.
 * @param decoder Pointer to URDecoder instance
 * @return Number of unique received fragment indexes
 */
size_t ur_decoder_received_parts_count(ur_decoder_t *decoder);

/**
 * Get estimated completion percentage
 * @param decoder Pointer to URDecoder instance
 * @return Completion percentage (0.0 to 1.0)
 */
float ur_decoder_estimated_percent_complete(ur_decoder_t *decoder);

/**
 * Get estimated completion percentage using the weighted-mixed-frames method.
 * @param decoder Pointer to URDecoder instance
 * @return Completion percentage (0.0 to 1.0)
 */
float ur_decoder_estimated_percent_complete_weighted(ur_decoder_t *decoder);

void ur_result_free(ur_result_t *result);

#endif // UR_DECODER_H