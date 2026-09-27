/*
 * test_ur_negative.c
 *
 * Negative-path tests: NULL / empty inputs, malformed UR strings,
 * bad CRC, truncated fragment, malformed CBOR. Every assertion
 * verifies that the API rejects cleanly and does not crash. Running
 * this under `make DEBUG=1 test` also checks memory safety on every
 * rejection path via ASan/UBSan.
 */

#include "../src/bytewords.h"
#include "../src/fountain_decoder.h"
#include "../src/fountain_encoder.h"
#include "../src/fountain_types.h"
#include "../src/types/bip39.h"
#include "../src/types/bytes_type.h"
#include "../src/types/cbor_encoder.h"
#include "../src/types/output.h"
#include "../src/types/psbt.h"
#include "../src/ur.h"
#include "../src/ur_decoder.h"
#include "../src/ur_encoder.h"
#include <stdbool.h>
#include <stdint.h>
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

// A known-good multi-part UR fragment from tests/test_cases/bytes/bytes_2.
// Lowercased so mutations below can flip letters predictably.
static const char VALID_FRAGMENT[] =
    "ur:bytes/26-6/lpcscyamcfadflcyvapeswvthdemjtfzjkimchhtkpinkkhyjocyhfckiol"
    "bheatcldaeedkbgaecmhyflioahkncpaecskpbsdndmetiheefzjofsceatfgcxhdcacfcp"
    "dagwesckwpmomnos";

static char *dup_fragment(void) {
  size_t n = strlen(VALID_FRAGMENT) + 1;
  char *s = malloc(n);
  if (s)
    memcpy(s, VALID_FRAGMENT, n);
  return s;
}

static void test_null_and_empty(void) {
  printf("\n=== null_and_empty ===\n");
  ur_decoder_t *d = ur_decoder_new();
  ASSERT(d != NULL, "decoder_new returns non-NULL");

  ASSERT(ur_decoder_get_state(d) == UR_DECODER_PROCESSING,
         "fresh decoder state is PROCESSING");
  ASSERT(ur_decoder_get_state(NULL) == UR_DECODER_ERROR_NULL_POINTER,
         "get_state(NULL) returns NULL_POINTER");

  ASSERT(ur_decoder_receive_part(d, NULL) == UR_DECODER_ERROR_NULL_POINTER,
         "receive_part rejects NULL string");
  ASSERT(ur_decoder_get_state(d) == UR_DECODER_ERROR_NULL_POINTER,
         "  -> state reflects NULL_POINTER");

  ASSERT(ur_decoder_receive_part(NULL, VALID_FRAGMENT) ==
             UR_DECODER_ERROR_NULL_POINTER,
         "receive_part rejects NULL decoder");

  ASSERT(ur_decoder_receive_part(d, "") == UR_DECODER_ERROR_INVALID_SCHEME,
         "receive_part rejects empty string");

  ur_decoder_free(d);
  ur_decoder_free(NULL); // no-op, must not crash

  ASSERT(bytes_from_cbor(NULL, 0) == NULL,
         "bytes_from_cbor(NULL, 0) returns NULL");
  ASSERT(bytes_from_cbor(NULL, 100) == NULL,
         "bytes_from_cbor(NULL, 100) returns NULL");
  uint8_t buf[1] = {0};
  ASSERT(bytes_from_cbor(buf, 0) == NULL,
         "bytes_from_cbor(buf, 0) returns NULL");
}

static void test_malformed_ur(void) {
  printf("\n=== malformed_ur ===\n");
  const char *cases[] = {
      "not-a-ur-at-all",       "http://bytes/abc", "ur:", "ur:/foo",
      "ur:bytes/zzzzzzzzzzzz", // not valid bytewords
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    ur_decoder_t *d = ur_decoder_new();
    ur_decoder_state_t state = ur_decoder_receive_part(d, cases[i]);
    ASSERT(ur_decoder_state_is_error(state), cases[i]);
    ur_decoder_free(d);
  }
}

static void test_bad_crc(void) {
  printf("\n=== bad_crc ===\n");
  char *mutated = dup_fragment();
  ASSERT(mutated != NULL, "dup_fragment");

  // Flip a letter deep in the bytewords body. Bytewords is 4-letter words
  // where only the 1st and 4th letter are used; swapping either changes the
  // decoded byte, so the trailing CRC32 mismatches and the fragment is
  // rejected.
  size_t body_start = strlen("ur:bytes/26-6/");
  size_t mid = body_start + (strlen(mutated) - body_start) / 2;
  mutated[mid] = (mutated[mid] == 'a' ? 'b' : 'a');

  ur_decoder_t *d = ur_decoder_new();
  ASSERT(ur_decoder_state_is_error(ur_decoder_receive_part(d, mutated)),
         "rejects fragment with flipped letter");

  // Transient errors clear on the next receive_part: feeding a pristine
  // fragment into the same decoder resumes decoding.
  ASSERT(ur_decoder_receive_part(d, VALID_FRAGMENT) == UR_DECODER_PROCESSING,
         "pristine fragment after transient error returns PROCESSING");

  ur_decoder_free(d);
  free(mutated);
}

static void test_truncated_fragment(void) {
  printf("\n=== truncated_fragment ===\n");
  char *truncated = dup_fragment();
  ASSERT(truncated != NULL, "dup_fragment");

  size_t n = strlen(truncated);
  ASSERT(n > 20, "fragment long enough to truncate");
  // Cut the last 20 chars — removes the trailing CRC32 region entirely.
  truncated[n - 20] = '\0';

  ur_decoder_t *d = ur_decoder_new();
  ASSERT(ur_decoder_state_is_error(ur_decoder_receive_part(d, truncated)),
         "rejects truncated fragment");
  ur_decoder_free(d);
  free(truncated);
}

// Regression for the empty-fragment double-free: a multi-part UR whose
// CBOR body carries a zero-length byte string (head 0x40). The old
// zero-copy path called safe_realloc(cbor_data, 0) which on glibc/musl
// frees the buffer and returns NULL, leaving fragment_data dangling and
// causing a double-free on the create_fountain_part_from_cbor fail
// branch. Must reject cleanly.
static void test_empty_fragment_payload(void) {
  printf("\n=== empty_fragment_payload ===\n");
  // CBOR: [seq_num=1, seq_len=1, message_len=1, checksum=0, h''].
  uint8_t cbor[] = {0x85, 0x01, 0x01, 0x01, 0x00, 0x40};
  char *bytewords = NULL;
  ASSERT(bytewords_encode(cbor, sizeof(cbor), &bytewords),
         "bytewords_encode crafted fragment");

  size_t n = strlen("ur:bytes/1-1/") + strlen(bytewords) + 1;
  char *ur = malloc(n);
  ASSERT(ur != NULL, "alloc ur string");
  snprintf(ur, n, "ur:bytes/1-1/%s", bytewords);

  ur_decoder_t *d = ur_decoder_new();
  ASSERT(ur_decoder_receive_part(d, ur) == UR_DECODER_ERROR_INVALID_FRAGMENT,
         "rejects empty-byte-string fragment payload");
  ASSERT(ur_decoder_get_state(d) == UR_DECODER_ERROR_INVALID_FRAGMENT,
         "  -> state agrees with returned INVALID_FRAGMENT");

  ur_decoder_free(d);
  free(bytewords);
  free(ur);
}

// A 1-of-1 multipart fragment whose CBOR checksum field is deliberately
// wrong: the fountain layer completes, verification fails, and the decoder
// must land in the terminal INVALID_CHECKSUM state and stay there.
static void test_checksum_terminal(void) {
  printf("\n=== checksum_terminal ===\n");
  // CBOR: [seq_num=1, seq_len=1, message_len=4, checksum=0, h'DEADBEEF'].
  // CRC32 of DEADBEEF is not 0, so verification fails.
  uint8_t cbor[] = {0x85, 0x01, 0x01, 0x04, 0x00, 0x44, 0xDE, 0xAD, 0xBE, 0xEF};
  char *bytewords = NULL;
  ASSERT(bytewords_encode(cbor, sizeof(cbor), &bytewords),
         "bytewords_encode crafted fragment");

  size_t n = strlen("ur:bytes/1-1/") + strlen(bytewords) + 1;
  char *ur = malloc(n);
  ASSERT(ur != NULL, "alloc ur string");
  snprintf(ur, n, "ur:bytes/1-1/%s", bytewords);

  ur_decoder_t *d = ur_decoder_new();
  ASSERT(ur_decoder_receive_part(d, ur) == UR_DECODER_ERROR_INVALID_CHECKSUM,
         "bad-checksum completing frame returns INVALID_CHECKSUM");
  ASSERT(ur_decoder_get_result(d) == NULL, "no result after checksum failure");
  ASSERT(ur_decoder_receive_part(d, VALID_FRAGMENT) ==
             UR_DECODER_ERROR_INVALID_CHECKSUM,
         "terminal state sticks: further parts are not processed");
  ASSERT(ur_decoder_receive_part(d, NULL) == UR_DECODER_ERROR_INVALID_CHECKSUM,
         "terminal state beats NULL input");

  ur_decoder_free(d);
  free(bytewords);
  free(ur);
}

// A valid single-part UR reaches the terminal OK state: the result is
// guaranteed non-NULL and further parts are ignored.
static void test_ok_terminal(void) {
  printf("\n=== ok_terminal ===\n");
  // CBOR byte string h'DEADBEEF' as the single-part payload.
  uint8_t cbor[] = {0x44, 0xDE, 0xAD, 0xBE, 0xEF};
  char *bytewords = NULL;
  ASSERT(bytewords_encode(cbor, sizeof(cbor), &bytewords),
         "bytewords_encode single-part payload");

  size_t n = strlen("ur:bytes/") + strlen(bytewords) + 1;
  char *ur = malloc(n);
  ASSERT(ur != NULL, "alloc ur string");
  snprintf(ur, n, "ur:bytes/%s", bytewords);

  ur_decoder_t *d = ur_decoder_new();
  ASSERT(ur_decoder_receive_part(d, ur) == UR_DECODER_OK,
         "valid single-part UR returns OK");
  ASSERT(ur_decoder_get_state(d) == UR_DECODER_OK, "state polls as OK");
  ASSERT(ur_decoder_get_result(d) != NULL, "OK guarantees non-NULL result");
  ASSERT(ur_decoder_receive_part(d, VALID_FRAGMENT) == UR_DECODER_OK,
         "terminal OK sticks: further parts are not processed");

  ur_decoder_free(d);
  free(bytewords);
  free(ur);
}

static void test_malformed_cbor(void) {
  printf("\n=== malformed_cbor ===\n");

  // Major type 2 (byte string), says "1-byte length follows" but buffer ends.
  uint8_t truncated_len[] = {0x58};
  ASSERT(bytes_from_cbor(truncated_len, sizeof(truncated_len)) == NULL,
         "rejects CBOR with missing length byte");

  // Declares 0x20 (32) bytes of body, but buffer has 0 body bytes.
  uint8_t len_overrun[] = {0x58, 0x20};
  ASSERT(bytes_from_cbor(len_overrun, sizeof(len_overrun)) == NULL,
         "rejects CBOR with length exceeding buffer");

  // Major type 4 (array) — valid CBOR, but wrong type for bytes.
  uint8_t wrong_type[] = {0x80};
  ASSERT(bytes_from_cbor(wrong_type, sizeof(wrong_type)) == NULL,
         "rejects CBOR with wrong major type for bytes");

  uint8_t psbt_trailing[] = {0x41, 0x00, 0x00};
  ASSERT(psbt_from_cbor(psbt_trailing, sizeof(psbt_trailing)) == NULL,
         "psbt_from_cbor rejects trailing bytes");
}

// Regression for the fountain-layer heap out-of-bounds read in
// reduce_part_by_part. Every part of a message carries the same padded
// fragment length; a malformed stream that varies it (a short degree-1 part
// {0} establishing the length, then a longer part whose fragment set contains
// 0) used to reach the XOR reduction, where ur_xor read the longer part's
// length out of the shorter stored part's buffer. The decoder must reject any
// length-mismatched part at receive time so the reduction is never reached.
// Under `make DEBUG=1 test` (ASan) this also proves there is no OOB access.
static void test_fountain_fragment_length_mismatch(void) {
  printf("\n=== fountain_fragment_length_mismatch ===\n");
  const size_t short_len = 10, long_len = 4000;
  const uint32_t checksum = 0x11223344u;

  // Sweep second-part sequence numbers: for at least one of them the decoder's
  // fragment selection yields a set that strictly contains the stored {0},
  // which is what drives reduce_part_by_part. The first part fixes the
  // expected fragment length at short_len, so every long_len part must be
  // rejected regardless of its index set.
  int trials = 0, rejected = 0, seeded = 0;
  for (uint32_t seq = 2; seq < 400; seq++) {
    fountain_decoder_t *d = fountain_decoder_new();

    fountain_encoder_part_t simple = {0};
    simple.seq_num = 1;
    simple.seq_len = 2;
    simple.message_len = 2 * short_len;
    simple.checksum = checksum;
    simple.data = calloc(short_len, 1);
    simple.data_len = short_len;
    // degree-1 {0}, establishes the expected fragment length
    if (fountain_decoder_receive_part(d, &simple))
      seeded++;
    fountain_encoder_part_free(&simple);

    fountain_encoder_part_t longer = {0};
    longer.seq_num = seq;
    longer.seq_len = 2;
    longer.message_len = 2 * short_len;
    longer.checksum = checksum;
    longer.data = calloc(long_len, 1);
    longer.data_len = long_len;
    bool accepted = fountain_decoder_receive_part(d, &longer);
    fountain_encoder_part_free(&longer);

    trials++;
    if (!accepted)
      rejected++;

    fountain_decoder_free(d);
  }

  ASSERT(seeded == trials, "every seed part was accepted");
  ASSERT(rejected == trials,
         "length-mismatched fountain parts are all rejected (no OOB reduce)");
}

static void test_ur_new_type_validation(void) {
  printf("\n=== ur_new_type_validation ===\n");
  const uint8_t cbor[] = {0x41, 0x01};

  ur_t *u = ur_new("crypto-psbt", cbor, sizeof(cbor));
  ASSERT(u != NULL, "ur_new accepts a valid type");
  ur_free(u);

  ASSERT(ur_new("BYTES", cbor, sizeof(cbor)) == NULL,
         "ur_new rejects uppercase type");
  ASSERT(ur_new("with space", cbor, sizeof(cbor)) == NULL,
         "ur_new rejects type with space");
  ASSERT(ur_new("", cbor, sizeof(cbor)) == NULL, "ur_new rejects empty type");
  ASSERT(ur_new("-bytes", cbor, sizeof(cbor)) == NULL,
         "ur_new rejects leading dash");
  ASSERT(ur_new("bytes-", cbor, sizeof(cbor)) == NULL,
         "ur_new rejects trailing dash");

  char *single = NULL;
  ASSERT(!ur_encoder_encode_single("with space", cbor, sizeof(cbor), &single),
         "ur_encoder_encode_single rejects an invalid type");
  ASSERT(single == NULL, "failed single-part encoding leaves output NULL");

  ASSERT(ur_encoder_new("with space", cbor, sizeof(cbor), 10, 0, 1) == NULL,
         "ur_encoder_new rejects an invalid type");
}

static void test_fountain_fragment_floor(void) {
  printf("\n=== fountain_fragment_floor ===\n");
  uint8_t msg[64];
  memset(msg, 0xAB, sizeof(msg));

  ASSERT(fountain_encoder_new(msg, sizeof(msg), 9, 0, 1) == NULL,
         "fountain_encoder_new rejects max_fragment_len < 10");
  fountain_encoder_t *e = fountain_encoder_new(msg, sizeof(msg), 10, 0, 1);
  ASSERT(e != NULL, "fountain_encoder_new accepts max_fragment_len == 10");
  fountain_encoder_free(e);
}

static void test_fountain_seq_num_no_wrap(void) {
  printf("\n=== fountain_seq_num_no_wrap ===\n");
  uint8_t msg[64];
  memset(msg, 0xAB, sizeof(msg));

  // Starting one below the ceiling still yields exactly one part; the next
  // call would wrap seq_num to 0 (an invalid part number that also asks
  // choose_fragments for fragment index (uint32_t)-1), so it must fail.
  fountain_encoder_t *e =
      fountain_encoder_new(msg, sizeof(msg), 20, UINT32_MAX - 1, 10);
  ASSERT(e != NULL, "fountain_encoder_new accepts first_seq_num near the top");
  if (e) {
    fountain_encoder_part_t part;
    memset(&part, 0, sizeof(part));
    ASSERT(fountain_encoder_next_part(e, &part),
           "next_part succeeds at seq_num == UINT32_MAX");
    fountain_encoder_part_free(&part);
    ASSERT(!fountain_encoder_next_part(e, &part),
           "next_part refuses to wrap seq_num past UINT32_MAX");
    fountain_encoder_free(e);
  }
}

static void test_empty_bytes_cbor_roundtrip(void) {
  printf("\n=== empty_bytes_cbor_roundtrip ===\n");
  // Regression: 0x40 (empty byte string) must decode. safe_malloc(0) returns
  // NULL, which decode_bytes used to misread as OOM, so bytes_from_cbor
  // rejected the exact CBOR bytes_to_cbor produced for an empty payload.
  bytes_data_t *b = bytes_new(NULL, 0);
  ASSERT(b != NULL, "bytes_new accepts an empty payload");

  size_t cbor_len = 0;
  uint8_t *cbor = b ? bytes_to_cbor(b, &cbor_len) : NULL;
  bytes_free(b);
  ASSERT(cbor != NULL && cbor_len == 1 && cbor[0] == 0x40,
         "empty Bytes encodes to 0x40");

  bytes_data_t *back = cbor ? bytes_from_cbor(cbor, cbor_len) : NULL;
  ASSERT(back != NULL, "empty Bytes CBOR (0x40) decodes");
  if (back) {
    size_t len = 99;
    (void)bytes_get_data(back, &len);
    ASSERT(len == 0, "decoded empty Bytes has length 0");
    bytes_free(back);
  }
  free(cbor);
}

// --- helpers for hand-crafting multipart UR strings ---------------------

static void cbor_put_uint(uint8_t *buf, size_t *len, uint64_t v) {
  if (v < 24) {
    buf[(*len)++] = (uint8_t)v;
  } else if (v <= 0xff) {
    buf[(*len)++] = 0x18;
    buf[(*len)++] = (uint8_t)v;
  } else if (v <= 0xffff) {
    buf[(*len)++] = 0x19;
    buf[(*len)++] = (uint8_t)(v >> 8);
    buf[(*len)++] = (uint8_t)v;
  } else {
    buf[(*len)++] = 0x1a;
    buf[(*len)++] = (uint8_t)(v >> 24);
    buf[(*len)++] = (uint8_t)(v >> 16);
    buf[(*len)++] = (uint8_t)(v >> 8);
    buf[(*len)++] = (uint8_t)v;
  }
}

// Build "ur:bytes/<seq_num>-<seq_len>/<bytewords>" carrying the given header
// fields and a zero-filled fragment of fragment_len bytes. Caller frees.
static char *make_multipart_ur(uint32_t seq_num, size_t seq_len,
                               size_t message_len, uint32_t checksum,
                               size_t fragment_len) {
  uint8_t *cbor = malloc(fragment_len + 64);
  if (!cbor)
    return NULL;
  size_t n = 0;
  cbor[n++] = 0x85; // array(5)
  cbor_put_uint(cbor, &n, seq_num);
  cbor_put_uint(cbor, &n, seq_len);
  cbor_put_uint(cbor, &n, message_len);
  cbor_put_uint(cbor, &n, checksum);
  if (fragment_len < 24) {
    cbor[n++] = (uint8_t)(0x40 | fragment_len);
  } else if (fragment_len <= 0xff) {
    cbor[n++] = 0x58;
    cbor[n++] = (uint8_t)fragment_len;
  } else if (fragment_len <= 0xffff) {
    cbor[n++] = 0x59;
    cbor[n++] = (uint8_t)(fragment_len >> 8);
    cbor[n++] = (uint8_t)fragment_len;
  } else {
    // 4-byte length. The 0x59 form above tops out at 65535, and silently
    // truncating past that made the declared length disagree with the bytes
    // actually appended - a malformed vector masquerading as an over-size one.
    cbor[n++] = 0x5a;
    cbor[n++] = (uint8_t)(fragment_len >> 24);
    cbor[n++] = (uint8_t)(fragment_len >> 16);
    cbor[n++] = (uint8_t)(fragment_len >> 8);
    cbor[n++] = (uint8_t)fragment_len;
  }
  memset(cbor + n, 0, fragment_len);
  n += fragment_len;

  char *bw = NULL;
  bool ok = bytewords_encode(cbor, n, &bw);
  free(cbor);
  if (!ok || !bw)
    return NULL;

  size_t out_len = strlen(bw) + 64;
  char *out = malloc(out_len);
  if (out)
    snprintf(out, out_len, "ur:bytes/%u-%zu/%s", seq_num, seq_len, bw);
  free(bw);
  return out;
}

static ur_decoder_state_t feed_one(const char *ur) {
  ur_decoder_t *d = ur_decoder_new();
  ur_decoder_state_t s = ur_decoder_receive_part(d, ur);
  ur_decoder_free(d);
  return s;
}

// Regression for the uninitialised-heap read at fountain reassembly. Nothing
// tied a fragment's length to the declared message_len, so a frame could claim
// a message far larger than its fragments supply; reassembly then allocated
// message_len uninitialised bytes, filled in only what arrived, and CRC'd the
// whole buffer. Under ASan/UBSan this also exercises the rejection paths.
static void test_multipart_geometry(void) {
  printf("\n=== multipart_geometry ===\n");
  const uint32_t crc = 0x11223344u;

  // The reported case: one tiny fragment claiming a 256 KiB message. Rejected
  // outright, and in particular never reaches the 256 KiB alloc + CRC.
  char *ur = make_multipart_ur(1, 1, 262144, crc, 1);
  ASSERT(ur != NULL, "built the oversized-geometry fragment");
  if (ur) {
    ur_decoder_state_t s = feed_one(ur);
    ASSERT(s != UR_DECODER_OK, "1-byte fragment claiming 256KiB is not OK");
    ASSERT(ur_decoder_state_is_error(s),
           "1-byte fragment claiming 256KiB is rejected");
    free(ur);
  }

  // A smaller mismatch that would still under-fill the reassembly buffer.
  ur = make_multipart_ur(1, 2, 100000, crc, 1);
  if (ur) {
    ASSERT(ur_decoder_state_is_error(feed_one(ur)),
           "fragment far shorter than ceil(message_len/seq_len) is rejected");
    free(ur);
  }

  // Correct geometry must still be accepted: ceil(10/2) == 5.
  ur = make_multipart_ur(1, 2, 10, crc, 5);
  if (ur) {
    ASSERT(feed_one(ur) == UR_DECODER_PROCESSING,
           "correct geometry (ceil(10/2)==5) is accepted");
    free(ur);
  }

  // Off by one in either direction is not.
  ur = make_multipart_ur(1, 2, 10, crc, 4);
  if (ur) {
    ASSERT(ur_decoder_state_is_error(feed_one(ur)),
           "fragment one byte short of the padded length is rejected");
    free(ur);
  }
  ur = make_multipart_ur(1, 2, 10, crc, 6);
  if (ur) {
    ASSERT(ur_decoder_state_is_error(feed_one(ur)),
           "fragment one byte over the padded length is rejected");
    free(ur);
  }

  // Rounding up must be honoured: ceil(11/2) == 6, not 5.
  ur = make_multipart_ur(1, 2, 11, crc, 6);
  if (ur) {
    ASSERT(feed_one(ur) == UR_DECODER_PROCESSING,
           "correct geometry with rounding (ceil(11/2)==6) is accepted");
    free(ur);
  }
}

// A message beyond the configured caps can never complete, however long the
// caller keeps scanning, so it must be reported as terminal rather than as the
// same transient error a misread frame produces.
static void test_unsupported_size_is_terminal(void) {
  printf("\n=== unsupported_size_is_terminal ===\n");
  const uint32_t crc = 0x11223344u;

  char *ur = make_multipart_ur(1, 2000, 20000, crc, 10);
  ASSERT(ur != NULL, "built the over-cap seq_len fragment");
  if (ur) {
    ur_decoder_t *d = ur_decoder_new();
    ur_decoder_state_t s = ur_decoder_receive_part(d, ur);
    ASSERT(s == UR_DECODER_ERROR_UNSUPPORTED_SIZE,
           "seq_len over the cap reports UNSUPPORTED_SIZE");
    ASSERT(ur_decoder_state_is_terminal(s), "UNSUPPORTED_SIZE is terminal");
    ASSERT(ur_decoder_state_is_error(s), "UNSUPPORTED_SIZE is an error");
    // Sticky: further parts must not clear it.
    ASSERT(ur_decoder_receive_part(d, VALID_FRAGMENT) ==
               UR_DECODER_ERROR_UNSUPPORTED_SIZE,
           "UNSUPPORTED_SIZE survives a subsequent valid part");
    ur_decoder_free(d);
    free(ur);
  }

  // Over-size message_len, with geometry that is otherwise self-consistent.
  ur = make_multipart_ur(1, 2, 300000, crc, 150000);
  if (ur) {
    ASSERT(feed_one(ur) == UR_DECODER_ERROR_UNSUPPORTED_SIZE,
           "message_len over the cap reports UNSUPPORTED_SIZE");
    free(ur);
  }

  // A genuinely malformed frame must stay transient, so callers can keep
  // scanning through misreads.
  ASSERT(!ur_decoder_state_is_terminal(feed_one("ur:bytes/1-2/notbytewords")),
         "a malformed fragment stays transient");

  // ...including when its path happens to declare an over-cap sequence length.
  // The terminal decision must rest on a frame that was actually decoded, not
  // on a path component: a misread of an ordinary frame can corrupt the digits
  // just as easily as the body, and a terminal state is sticky.
  ASSERT(
      !ur_decoder_state_is_terminal(feed_one("ur:bytes/1-1025/notbytewords")),
      "over-cap seq_len with an undecodable body stays transient");

  // Same for a well-formed body whose geometry does not add up: malformed
  // wins over over-size, because only one of the two is a reason to stop.
  char *bad = make_multipart_ur(1, 2000, 20000, crc, 9);
  if (bad) {
    ASSERT(!ur_decoder_state_is_terminal(feed_one(bad)),
           "over-cap seq_len with bad geometry stays transient");
    free(bad);
  }
}

// --- crypto-output hd-key fields ------------------------------------------

static void map_put(cbor_value_t *map, uint64_t key, cbor_value_t *value) {
  if (!cbor_map_set(map, cbor_value_new_unsigned_int(key), value))
    abort();
}

static void array_push(cbor_value_t *array, cbor_value_t *item) {
  if (!cbor_array_append(array, item))
    abort();
}

// #6.304({1: [index, hardened, ...]}); UINT64_MAX stands for the wildcard.
static cbor_value_t *keypath_item(const uint64_t *index, const bool *hardened,
                                  size_t count) {
  cbor_value_t *components = cbor_value_new_array();
  for (size_t i = 0; i < count; i++) {
    array_push(components, index[i] == UINT64_MAX
                               ? cbor_value_new_array()
                               : cbor_value_new_unsigned_int(index[i]));
    array_push(components, cbor_value_new_bool(hardened[i]));
  }
  cbor_value_t *map = cbor_value_new_map();
  map_put(map, 1, components);
  return cbor_value_new_tag(CRYPTO_KEYPATH_TAG, map);
}

static const uint8_t PUBKEY[33] = {
    0x02, 0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac, 0x55, 0xa0,
    0x62, 0x95, 0xce, 0x87, 0x0b, 0x07, 0x02, 0x9b, 0xfc, 0xdb, 0x2d,
    0xce, 0x28, 0xd9, 0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8, 0x17, 0x98};
static const uint8_t CHAIN_CODE[32] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

// An hd-key map for [deadbeef/84'/0'/0']xpub/0/*, for tests to alter.
static cbor_value_t *hdkey_map(void) {
  static const uint64_t origin[] = {84, 0, 0};
  static const bool origin_hardened[] = {true, true, true};
  static const uint64_t children[] = {0, UINT64_MAX};
  static const bool children_hardened[] = {false, false};

  cbor_value_t *map = cbor_value_new_map();
  map_put(map, 3, cbor_value_new_bytes(PUBKEY, sizeof PUBKEY));
  map_put(map, 4, cbor_value_new_bytes(CHAIN_CODE, sizeof CHAIN_CODE));
  cbor_value_t *origin_path = keypath_item(origin, origin_hardened, 3);
  map_put(cbor_value_get_tag_content(origin_path), 2,
          cbor_value_new_unsigned_int(0xdeadbeef));
  map_put(map, 6, origin_path);
  map_put(map, 7, keypath_item(children, children_hardened, 2));
  return map;
}

typedef enum { REJECTED, NOT_RENDERED, RENDERED } hdkey_outcome_t;

// Decode wpkh(<map>) as a crypto-output and render it. Takes ownership.
static hdkey_outcome_t decode_wpkh(cbor_value_t *map, char **descriptor) {
  cbor_value_t *item = cbor_value_new_tag(
      SCRIPT_EXPR_WPKH, cbor_value_new_tag(CRYPTO_HDKEY_TAG, map));
  size_t len = 0;
  uint8_t *cbor = cbor_encode(item, &len);
  cbor_value_free(item);
  output_data_t *output = cbor ? output_from_cbor(cbor, len) : NULL;
  free(cbor);
  if (!output)
    return REJECTED;
  char *rendered = output_descriptor(output, true);
  output_free(output);
  if (descriptor)
    *descriptor = rendered;
  else
    free(rendered);
  return rendered ? RENDERED : NOT_RENDERED;
}

static void test_hdkey_fields_fail_closed(void) {
  printf("\n=== hdkey_fields_fail_closed ===\n");

  char *descriptor = NULL;
  ASSERT(decode_wpkh(hdkey_map(), &descriptor) == RENDERED && descriptor &&
             strncmp(descriptor, "wpkh([deadbeef/84'/0'/0']xpub", 29) == 0 &&
             strstr(descriptor, "/0/*)#"),
         "well-formed key renders with origin and children");
  free(descriptor);

  cbor_value_t *map = hdkey_map();
  map_put(map, 4, cbor_value_new_bytes(CHAIN_CODE, 31));
  ASSERT(decode_wpkh(map, NULL) == REJECTED, "31-byte chain code rejected");

  map = hdkey_map();
  map_put(map, 4, cbor_value_new_unsigned_int(7));
  ASSERT(decode_wpkh(map, NULL) == REJECTED, "non-bytes chain code rejected");

  cbor_value_t *no_chain = cbor_value_new_map();
  map_put(no_chain, 3, cbor_value_new_bytes(PUBKEY, sizeof PUBKEY));
  ASSERT(decode_wpkh(no_chain, NULL) == NOT_RENDERED,
         "key without chain code is not rendered as an xpub");

  static const uint64_t odd_index[] = {0};
  static const bool odd_hardened[] = {false};
  map = hdkey_map();
  cbor_value_t *odd = keypath_item(odd_index, odd_hardened, 1);
  array_push(get_map_value(cbor_value_get_tag_content(odd), 1),
             cbor_value_new_unsigned_int(1));
  map_put(map, 7, odd);
  ASSERT(decode_wpkh(map, NULL) == REJECTED,
         "malformed children path rejected, not dropped");

  map = hdkey_map();
  map_put(
      map, 6,
      cbor_value_new_tag(CRYPTO_KEYPATH_TAG, cbor_value_new_unsigned_int(7)));
  ASSERT(decode_wpkh(map, NULL) == REJECTED,
         "malformed origin rejected, not dropped");

  map = hdkey_map();
  map_put(map, 8, cbor_value_new_unsigned_int(0x100000000ull));
  ASSERT(decode_wpkh(map, NULL) == REJECTED,
         "parent fingerprint above 32 bits rejected");

  map = hdkey_map();
  map_put(map, 2, cbor_value_new_bool(true));
  ASSERT(decode_wpkh(map, NULL) == REJECTED, "is-private key rejected");

  map = hdkey_map();
  map_put(map, 1, cbor_value_new_unsigned_int(1));
  ASSERT(decode_wpkh(map, NULL) == REJECTED, "non-bool is-master rejected");

  uint8_t private_data[33] = {0};
  memcpy(private_data + 1, CHAIN_CODE, 32);
  map = hdkey_map();
  map_put(map, 3, cbor_value_new_bytes(private_data, sizeof private_data));
  ASSERT(decode_wpkh(map, NULL) == NOT_RENDERED,
         "private key-data is not rendered as an xpub");

  map = hdkey_map();
  map_put(map, 3, cbor_value_new_bytes(PUBKEY + 1, 32));
  ASSERT(decode_wpkh(map, NULL) == NOT_RENDERED,
         "32-byte key-data is not rendered as an xpub");
}

static void test_fountain_part_validation(void) {
  printf("\n=== fountain_part_validation ===\n");

  uint8_t fragment[8] = {0};
  struct {
    uint32_t seq_num;
    size_t seq_len, message_len, data_len;
    bool accepted;
    const char *what;
  } cases[] = {
      {1, 2, 16, 8, true, "consistent geometry accepted"},
      {0, 2, 16, 8, false, "seq_num 0 rejected"},
      {1, 2, 20, 8, false, "fragments shorter than the message rejected"},
      {1, 2, 10, 8, false, "fragments longer than ceil(len/seq) rejected"},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    fountain_decoder_t *d = fountain_decoder_new();
    fountain_encoder_part_t part = {0};
    part.seq_num = cases[i].seq_num;
    part.seq_len = cases[i].seq_len;
    part.message_len = cases[i].message_len;
    part.checksum = 1;
    part.data = malloc(cases[i].data_len);
    part.data_len = cases[i].data_len;
    if (part.data)
      memcpy(part.data, fragment, cases[i].data_len);
    ASSERT(fountain_decoder_receive_part(d, &part) == cases[i].accepted,
           cases[i].what);
    fountain_encoder_part_free(&part);
    fountain_decoder_free(d);
  }
}

static void test_cbor_ambiguity_rejected(void) {
  printf("\n=== cbor_ambiguity_rejected ===\n");

  // {1: ["a"]}
  const uint8_t words[] = {0xa1, 0x01, 0x81, 0x61, 'a'};
  bip39_data_t *bip39 = bip39_from_cbor(words, sizeof words);
  ASSERT(bip39 != NULL, "well-formed map decodes");
  bip39_free(bip39);

  // {1: ["a"], 1: ["b"]}
  const uint8_t repeated[] = {0xa2, 0x01, 0x81, 0x61, 'a',
                              0x01, 0x81, 0x61, 'b'};
  ASSERT(bip39_from_cbor(repeated, sizeof repeated) == NULL,
         "repeated map key rejected");

  const uint8_t trailing[] = {0xa1, 0x01, 0x81, 0x61, 'a', 0x00};
  ASSERT(bip39_from_cbor(trailing, sizeof trailing) == NULL,
         "trailing bytes after the item rejected");
}

// Path and sequence components split the way the reference decoders do.
static void test_path_components(void) {
  printf("\n=== path_components ===\n");

  uint8_t message[50];
  for (size_t i = 0; i < sizeof message; i++)
    message[i] = (uint8_t)i;
  ur_encoder_t *enc =
      ur_encoder_new("bytes", message, sizeof message, 20, 0, 10);
  char *part = NULL;
  char *single = NULL;
  if (!enc || !ur_encoder_next_part(enc, &part) ||
      !ur_encoder_encode_single("bytes", message, 20, &single)) {
    ASSERT(false, "built reference parts");
    ur_encoder_free(enc);
    free(part);
    free(single);
    return;
  }
  const char *body = strchr(part + strlen("UR:BYTES/"), '/') + 1;
  const char *single_body = single + strlen("UR:BYTES/");

  struct {
    const char *seq;
    bool accepted;
  } cases[] = {
      {"1-3", true},   {"+1-3", true},          {"1-3-99", false},
      {"1--3", false}, {"1-", false},           {"-3", false},
      {"1-3x", false}, {"4294967297-3", false}, {"1-4294967299", false}};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char frame[1024];
    snprintf(frame, sizeof frame, "ur:bytes/%s/%s", cases[i].seq, body);
    ur_decoder_t *d = ur_decoder_new();
    ur_decoder_state_t state = ur_decoder_receive_part(d, frame);
    ASSERT(cases[i].accepted ? state == UR_DECODER_PROCESSING
                             : ur_decoder_state_is_error(state),
           cases[i].seq);
    ur_decoder_free(d);
  }

  const char *layouts[] = {"ur:bytes//%s", "ur:bytes/%s/"};
  for (size_t i = 0; i < 2; i++) {
    char frame[1024];
    snprintf(frame, sizeof frame, layouts[i], single_body);
    ur_decoder_t *d = ur_decoder_new();
    ASSERT(ur_decoder_state_is_error(ur_decoder_receive_part(d, frame)),
           layouts[i]);
    ur_decoder_free(d);
  }

  free(part);
  free(single);
  ur_encoder_free(enc);
}

static cbor_value_t *hdkey_map_with_origin(const uint64_t *index,
                                           size_t count) {
  static const bool hardened[] = {true, true, true, true};
  cbor_value_t *map = hdkey_map();
  cbor_value_t *origin = keypath_item(index, hardened, count);
  map_put(cbor_value_get_tag_content(origin), 2,
          cbor_value_new_unsigned_int(0xdeadbeef));
  map_put(map, 6, origin);
  return map;
}

static void test_keypath_components_fail_closed(void) {
  printf("\n=== keypath_components_fail_closed ===\n");

  cbor_value_t *range = cbor_value_new_array();
  array_push(range, cbor_value_new_unsigned_int(0));
  array_push(range, cbor_value_new_unsigned_int(99));
  cbor_value_t *components = cbor_value_new_array();
  array_push(components, range);
  array_push(components, cbor_value_new_bool(false));
  cbor_value_t *children = cbor_value_new_map();
  map_put(children, 1, components);
  cbor_value_t *map = hdkey_map();
  map_put(map, 7, cbor_value_new_tag(CRYPTO_KEYPATH_TAG, children));
  ASSERT(decode_wpkh(map, NULL) == REJECTED,
         "child range rejected, not read as a wildcard");

  const uint64_t truncating[] = {0x100000000ull + 84, 0, 0};
  ASSERT(decode_wpkh(hdkey_map_with_origin(truncating, 3), NULL) == REJECTED,
         "index above 32 bits rejected, not truncated");

  const uint64_t hardened_range[] = {0x80000000ull, 0, 0};
  ASSERT(decode_wpkh(hdkey_map_with_origin(hardened_range, 3), NULL) ==
             REJECTED,
         "index at 2^31 rejected");

  map = hdkey_map();
  cbor_value_t *origin = get_map_value(map, 6);
  map_put(cbor_value_get_tag_content(origin), 2,
          cbor_value_new_unsigned_int(0x100000000ull));
  ASSERT(decode_wpkh(map, NULL) == REJECTED,
         "source fingerprint above 32 bits rejected");

  map = hdkey_map();
  origin = get_map_value(map, 6);
  map_put(cbor_value_get_tag_content(origin), 2,
          cbor_value_new_bytes(PUBKEY, 4));
  ASSERT(decode_wpkh(map, NULL) == REJECTED,
         "non-integer source fingerprint rejected");

  map = hdkey_map();
  origin = get_map_value(map, 6);
  map_put(cbor_value_get_tag_content(origin), 3,
          cbor_value_new_unsigned_int(256));
  ASSERT(decode_wpkh(map, NULL) == REJECTED, "depth above 255 rejected");

  // The descriptor-string parser takes uint31 indexes only, too.
  char *descriptor = NULL;
  if (decode_wpkh(hdkey_map(), &descriptor) == RENDERED && descriptor) {
    const char *at = strstr(descriptor, "/84'/");
    size_t prefix = at ? (size_t)(at - descriptor) + 1 : 0;
    char *hash = strchr(descriptor, '#');
    if (hash)
      *hash = '\0';
    char mutated[512];
    snprintf(mutated, sizeof mutated, "%.*s2147483648%s", (int)prefix,
             descriptor, at ? at + 3 : "");
    output_data_t *parsed = output_from_descriptor_string(mutated);
    ASSERT(at && !parsed, "descriptor index at 2^31 rejected");
    output_free(parsed);
    snprintf(mutated, sizeof mutated, "%.*s2147483647%s", (int)prefix,
             descriptor, at ? at + 3 : "");
    parsed = output_from_descriptor_string(mutated);
    ASSERT(at && parsed, "descriptor index 2^31 - 1 accepted");
    output_free(parsed);
  } else {
    ASSERT(false, "rendered the reference descriptor");
  }
  free(descriptor);
}

int main(void) {
  printf("=== UR Negative-Path Tests ===\n");
  test_null_and_empty();
  test_malformed_ur();
  test_bad_crc();
  test_truncated_fragment();
  test_empty_fragment_payload();
  test_checksum_terminal();
  test_ok_terminal();
  test_malformed_cbor();
  test_fountain_fragment_length_mismatch();
  test_ur_new_type_validation();
  test_fountain_fragment_floor();
  test_fountain_seq_num_no_wrap();
  test_empty_bytes_cbor_roundtrip();
  test_multipart_geometry();
  test_unsupported_size_is_terminal();
  test_fountain_part_validation();
  test_cbor_ambiguity_rejected();
  test_path_components();
  test_hdkey_fields_fail_closed();
  test_keypath_components_fail_closed();

  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/%d\n", asserts - failures, asserts);
  return failures == 0 ? 0 : 1;
}
