/*
 * test_ur_alloc.c
 *
 * Allocation behaviour, observed by wrapping the allocator at link time
 * (-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free, GNU ld only;
 * the Makefile builds this test on Linux).
 */

#include "../src/types/bip39.h"
#include "../src/types/bytes_type.h"
#include "../src/types/output.h"
#include "../src/types/psbt.h"
#include "../src/ur_decoder.h"
#include "../src/ur_encoder.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int asserts = 0;
static int failures = 0;

#define ASSERT(cond, msg)                                                      \
  do {                                                                         \
    asserts++;                                                                 \
    if (!(cond)) {                                                             \
      fprintf(stderr, "  FAIL @%d: %s\n", __LINE__, msg);                      \
      failures++;                                                              \
    } else {                                                                   \
      printf("  PASS %s\n", msg);                                              \
    }                                                                          \
  } while (0)

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *ptr, size_t size);
void __real_free(void *ptr);

static size_t fail_size; // fail the next malloc() of exactly this size
static size_t largest;   // largest malloc() since the last reset
static long fail_at;     // fail the allocation with this number; 0 for none
static long alloc_count; // allocations attempted since the last reset
static long live_blocks; // blocks allocated and not yet freed

// Every allocation is numbered, whichever entry point it comes through: the
// compiler is free to turn a malloc() + memset() pair into calloc().
static bool fail_this_allocation(void) {
  alloc_count++;
  return fail_at && alloc_count == fail_at;
}

void *__wrap_malloc(size_t size) {
  if (fail_this_allocation())
    return NULL;
  if (fail_size && size == fail_size) {
    fail_size = 0;
    return NULL;
  }
  if (size > largest)
    largest = size;
  void *block = __real_malloc(size);
  if (block)
    live_blocks++;
  return block;
}

void *__wrap_calloc(size_t count, size_t size) {
  if (fail_this_allocation())
    return NULL;
  void *block = __real_calloc(count, size);
  if (block)
    live_blocks++;
  return block;
}

// The library never passes a size of 0, so a realloc() only adds a block
// when it is given none.
void *__wrap_realloc(void *ptr, size_t size) {
  if (fail_this_allocation())
    return NULL;
  void *block = __real_realloc(ptr, size);
  if (block && !ptr)
    live_blocks++;
  return block;
}

void __wrap_free(void *ptr) {
  if (ptr)
    live_blocks--;
  __real_free(ptr);
}

static void test_reassembly_retry(void) {
  printf("\n=== reassembly_retry ===\n");

  // No other allocation made while decoding this message has this size, so
  // failing it hits exactly the reassembly buffer.
  enum { MESSAGE_LEN = 1237 };
  uint8_t message[MESSAGE_LEN];
  for (size_t i = 0; i < sizeof message; i++)
    message[i] = (uint8_t)(i * 31 + 7);

  ur_encoder_t *enc =
      ur_encoder_new("bytes", message, sizeof message, 100, 0, 10);
  ur_decoder_t *dec = ur_decoder_new();
  ASSERT(enc && dec, "created encoder and decoder");
  if (!enc || !dec) {
    ur_encoder_free(enc);
    ur_decoder_free(dec);
    return;
  }

  size_t seq_len = ur_encoder_seq_len(enc);
  ur_decoder_state_t state = UR_DECODER_PROCESSING;
  for (size_t i = 0; i <= seq_len; i++) {
    char *part = NULL;
    if (!ur_encoder_next_part(enc, &part))
      break;
    if (i == seq_len - 1)
      fail_size = MESSAGE_LEN;
    state = ur_decoder_receive_part(dec, part);
    free(part);
    if (i == seq_len - 1) {
      ASSERT(fail_size == 0, "the reassembly allocation was failed");
      ASSERT(state == UR_DECODER_ERROR_MEMORY,
             "failed reassembly reports a transient MEMORY error");
    }
  }

  ur_result_t *result = ur_decoder_get_result(dec);
  ASSERT(state == UR_DECODER_OK && result && result->cbor_len == MESSAGE_LEN &&
             memcmp(result->cbor_data, message, MESSAGE_LEN) == 0,
         "the next part completes the message");

  ur_decoder_free(dec);
  ur_encoder_free(enc);
}

// A string head declaring more bytes than the input holds must be rejected
// before anything that size is allocated.
static void test_cbor_length_checked_before_alloc(void) {
  printf("\n=== cbor_length_checked_before_alloc ===\n");

  const uint8_t bytes_head[] = {0x5a, 0x00, 0x04, 0x00, 0x00}; // 256 KiB
  largest = 0;
  bytes_data_t *bytes = bytes_from_cbor(bytes_head, sizeof bytes_head);
  ASSERT(!bytes, "truncated byte string is rejected");
  ASSERT(largest < 1024, "  -> without allocating its declared length");
  bytes_free(bytes);

  // {1: [<text of 256 KiB>]}, the text itself missing
  const uint8_t text_head[] = {0xa1, 0x01, 0x81, 0x7a, 0x00, 0x04, 0x00, 0x00};
  largest = 0;
  bip39_data_t *bip39 = bip39_from_cbor(text_head, sizeof text_head);
  ASSERT(!bip39, "truncated text string is rejected");
  ASSERT(largest < 1024, "  -> without allocating its declared length");
  bip39_free(bip39);
}

// The key at m/84'/1'/0' of the BIP32 test vector 1 seed.
#define TPUB                                                                   \
  "tpubDDNRbZGvdA33cgpY5uy2mmphT7sK4uciRjcQScSd64S5KRyZDxHcPuzs24or84Hywugb2J" \
  "bEEt2jWH8fduiN9cmZzkSj8sSSx6txXkhXyZs"

static const char *const DESCRIPTORS[] = {
    "wpkh([3442193e/84'/1'/0']" TPUB "/0/*)",
    "wsh(sortedmulti(2,[3442193e/84'/1'/0']" TPUB
    "/0/*,[08c3586c/84'/1'/0']" TPUB "/1/*))",
};
#define DESCRIPTOR_COUNT (sizeof DESCRIPTORS / sizeof DESCRIPTORS[0])

// Run `step` once to count its allocations, then once more per allocation with
// that one failing. `check` judges each run and frees what it produced; a run
// that leaves a block behind is a leak. Returns the number of runs that were
// rejected or leaked, or -1 if the run without failures was itself rejected.
static long sweep(void (*step)(void), bool (*check)(void)) {
  long before = live_blocks;
  fail_at = 0;
  alloc_count = 0;
  step();
  long total = alloc_count;
  if (!check() || live_blocks != before)
    return -1;

  long rejected = 0;
  for (long k = 1; k <= total; k++) {
    alloc_count = 0;
    fail_at = k;
    step();
    fail_at = 0;
    if (!check() || live_blocks != before)
      rejected++;
  }
  return rejected;
}

// --- encode: descriptor text -> crypto-output CBOR -------------------------

static const char *encode_text;
static uint8_t *encode_expected, *encode_cbor;
static size_t encode_expected_len, encode_len;

static void encode_step(void) {
  encode_len = 0;
  output_data_t *output = output_from_descriptor_string(encode_text);
  encode_cbor = output ? output_to_cbor(output, &encode_len) : NULL;
  output_free(output);
}

static bool encode_check(void) {
  bool ok = !encode_cbor ||
            (encode_len == encode_expected_len &&
             memcmp(encode_cbor, encode_expected, encode_len) == 0);
  free(encode_cbor);
  encode_cbor = NULL;
  return ok;
}

// --- decode: the simple types ----------------------------------------------

static bool simple_ok;

static void simple_step(void) {
  // {1: ["abc", "def"], 2: "en"}
  static const uint8_t words[] = {0xa2, 0x01, 0x82, 0x63, 'a',  'b', 'c', 0x63,
                                  'd',  'e',  'f',  0x02, 0x62, 'e', 'n'};
  static const uint8_t bytes[] = {0x45, 1, 2, 3, 4, 5};
  simple_ok = true;

  bip39_data_t *bip39 = bip39_from_cbor(words, sizeof words);
  size_t count = 0;
  char **list = bip39 ? bip39_get_words(bip39, &count) : NULL;
  if (bip39 && (count != 2 || strcmp(list[0], "abc")))
    simple_ok = false;
  bip39_free(bip39);

  bytes_data_t *data = bytes_from_cbor(bytes, sizeof bytes);
  size_t len = 0;
  uint8_t *cbor = data ? bytes_to_cbor(data, &len) : NULL;
  if (cbor && (len != sizeof bytes || memcmp(cbor, bytes, len)))
    simple_ok = false;
  free(cbor);
  bytes_free(data);

  registry_item_t *item =
      registry_item_from_cbor(bytes, sizeof bytes, psbt_from_data_item);
  if (item)
    psbt_free(psbt_from_registry_item(item));
  free(item);
}

static bool simple_check(void) { return simple_ok; }

// --- multi-part: encoder -> lossy channel -> decoder ------------------------

static uint8_t session_message[600];
static bool session_ok;

// A transient allocation failure may cost a frame, never the message: the
// stream keeps coming, so the decode must still finish, with the right bytes.
static void session_step(void) {
  session_ok = true;
  ur_encoder_t *enc = ur_encoder_new("bytes", session_message,
                                     sizeof session_message, 60, 0, 10);
  ur_decoder_t *dec = ur_decoder_new();
  if (enc && dec) {
    ur_decoder_state_t state = UR_DECODER_PROCESSING;
    for (int i = 0; i < 200 && !ur_decoder_state_is_terminal(state); i++) {
      char *part = NULL;
      if (!ur_encoder_next_part(enc, &part))
        continue;
      // Lose most of the pure fragments, so mixed parts do the work.
      if (i >= 10 || i % 3 == 0)
        state = ur_decoder_receive_part(dec, part);
      free(part);
      (void)ur_decoder_estimated_percent_complete_weighted(dec);
    }
    ur_result_t *result = ur_decoder_get_result(dec);
    session_ok = state == UR_DECODER_OK && result &&
                 result->cbor_len == sizeof session_message &&
                 memcmp(result->cbor_data, session_message,
                        sizeof session_message) == 0;
  }
  ur_decoder_free(dec);
  ur_encoder_free(enc);
}

static bool session_check(void) { return session_ok; }

// Every allocation in each of these paths is failed in turn. Whatever the
// outcome, nothing may leak and nothing wrong may be returned. Under
// `make DEBUG=1`, ASan also checks each failure path for memory errors.
static void test_alloc_failure_sweeps(void) {
  printf("\n=== alloc_failure_sweeps ===\n");

  for (size_t i = 0; i < DESCRIPTOR_COUNT; i++) {
    encode_text = DESCRIPTORS[i];
    output_data_t *output = output_from_descriptor_string(encode_text);
    encode_expected =
        output ? output_to_cbor(output, &encode_expected_len) : NULL;
    output_free(output);
    ASSERT(encode_expected && sweep(encode_step, encode_check) == 0,
           "descriptor text to CBOR");
    free(encode_expected);
  }

  ASSERT(sweep(simple_step, simple_check) == 0, "bip39, bytes and psbt");

  for (size_t i = 0; i < sizeof session_message; i++)
    session_message[i] = (uint8_t)(i * 131 + 17);
  ASSERT(sweep(session_step, session_check) == 0,
         "multi-part encode and lossy decode");
}

int main(void) {
  printf("=== UR Allocation Tests ===\n");
  test_reassembly_retry();
  test_cbor_length_checked_before_alloc();
  test_alloc_failure_sweeps();
  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/%d\n", asserts - failures, asserts);
  return failures == 0 ? 0 : 1;
}
