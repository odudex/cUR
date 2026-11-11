/*
 * test_ur_fuzz.c
 *
 * Randomised robustness tests with a fixed seed, so every run does the same
 * thing. Each generates inputs the hand-written suites do not enumerate and
 * checks properties that must hold for all of them:
 *
 *   - a CBOR payload either fails to decode or decodes to something that
 *     encodes and decodes again to the same descriptor;
 *   - descriptor text either fails to parse or survives a trip through CBOR;
 *   - a multi-part stream decodes to exactly the message that was encoded,
 *     through loss, reordering, repeats and hostile frames, and the decoder's
 *     state machine keeps its promises throughout.
 *
 * Run under `make DEBUG=1 test`, the same inputs also go through ASan and
 * UBSan. `tests/test_ur_fuzz <seed> <rounds>` explores other inputs.
 */

#include "../src/bytewords.h"
#include "../src/types/bip39.h"
#include "../src/types/bytes_type.h"
#include "../src/types/output.h"
#include "../src/types/psbt.h"
#include "../src/ur_decoder.h"
#include "../src/ur_encoder.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define FAIL(...)                                                              \
  do {                                                                         \
    if (failures++ < 10) {                                                     \
      fprintf(stderr, "  FAIL @%d: ", __LINE__);                               \
      fprintf(stderr, __VA_ARGS__);                                            \
      fprintf(stderr, "\n");                                                   \
    }                                                                          \
  } while (0)

// xorshift64: the tests need repeatable, not good, randomness.
static uint64_t rng_state;

static uint32_t rnd(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return (uint32_t)(rng_state >> 16);
}

// A number below n; 0 when n is 0.
static uint32_t below(uint32_t n) { return n ? rnd() % n : 0; }

// Keys at m/84'/0'/0' and m/84'/1'/0' of the BIP32 test vector 1 seed.
#define XPUB                                                                   \
  "xpub6C1HVMz946r433QEjZGpYYWYcspxXXBPys5PBGkmQboRXE6RLfFiStEkKbWKCZaPgDrzZh" \
  "9nUEunxuiuy6MNdw23du2Ek7GoKYMJVH8eK5E"
#define TPUB                                                                   \
  "tpubDDNRbZGvdA33cgpY5uy2mmphT7sK4uciRjcQScSd64S5KRyZDxHcPuzs24or84Hywugb2J" \
  "bEEt2jWH8fduiN9cmZzkSj8sSSx6txXkhXyZs"

static const char *const DESCRIPTORS[] = {
    "pkh([3442193e/84'/0'/0']" XPUB "/0/*)",
    "wpkh([3442193e/84h/1h/0h]" TPUB "/0/*)",
    "sh(wpkh([3442193e/84'/0'/0']" XPUB "))",
    "tr([3442193e/84'/0'/0']" XPUB "/0/*)",
    "wsh(sortedmulti(2,[3442193e/84'/0'/0']" XPUB
    "/0/*,[08c3586c/84'/0'/0']" XPUB "/1/*))",
    "sh(wsh(multi(1,[3442193e/84'/1'/0']" TPUB "/0/*,[08c3586c/84'/1'/0']" TPUB
    ")))",
};
#define DESCRIPTOR_COUNT (sizeof DESCRIPTORS / sizeof DESCRIPTORS[0])

// --- descriptors and their CBOR --------------------------------------------

// An output must encode, and what it encodes to must decode to the same
// descriptor. `what` and `input` describe the case for a failure report.
static void check_output_roundtrips(output_data_t *output, const char *what,
                                    const char *input) {
  char *rendered = output_descriptor(output, true);
  size_t len = 0;
  uint8_t *cbor = output_to_cbor(output, &len);
  output_data_t *again = cbor ? output_from_cbor(cbor, len) : NULL;
  char *rendered_again = again ? output_descriptor(again, true) : NULL;

  if (!cbor)
    FAIL("%s: accepted but does not encode: %s", what, input);
  else if (!again)
    FAIL("%s: its own encoding does not decode: %s", what, input);
  else if ((rendered == NULL) != (rendered_again == NULL) ||
           (rendered && strcmp(rendered, rendered_again) != 0))
    FAIL("%s: descriptor changed across CBOR: %s", what, input);

  free(rendered_again);
  output_free(again);
  free(cbor);
  free(rendered);
}

// Change, drop, insert or cut characters of a valid descriptor.
static void fuzz_descriptor_text(void) {
  static const char alphabet[] = "0123456789()[],'/*h#abcdefxptu<>; ";
  char text[1024];
  const char *seed = DESCRIPTORS[below(DESCRIPTOR_COUNT)];
  size_t len = strlen(seed);
  memcpy(text, seed, len + 1);

  for (uint32_t edits = 1 + below(3); edits > 0; edits--) {
    size_t at = below((uint32_t)len);
    switch (below(4)) {
    case 0:
      text[at] = alphabet[below(sizeof alphabet - 1)];
      break;
    case 1:
      if (len > 1) {
        memmove(text + at, text + at + 1, len - at);
        len--;
      }
      break;
    case 2:
      if (len < sizeof text - 2) {
        memmove(text + at + 1, text + at, len - at + 1);
        text[at] = alphabet[below(sizeof alphabet - 1)];
        len++;
      }
      break;
    default:
      len = at + 1;
      text[len] = '\0';
      break;
    }
  }

  output_data_t *output = output_from_descriptor_string(text);
  if (output)
    check_output_roundtrips(output, "descriptor text", text);
  output_free(output);
}

// Run every payload decoder over one buffer.
static void decode_everything(const uint8_t *data, size_t len) {
  bytes_free(bytes_from_cbor(data, len));
  psbt_free(psbt_from_cbor(data, len));

  bip39_data_t *bip39 = bip39_from_cbor(data, len);
  size_t count = 0;
  char **words = bip39 ? bip39_get_words(bip39, &count) : NULL;
  for (size_t i = 0; i < count; i++)
    (void)strlen(words[i]);
  bip39_free(bip39);

  output_data_t *output = output_from_cbor(data, len);
  if (output)
    check_output_roundtrips(output, "crypto-output", "(mutated CBOR)");
  output_free(output);

  char **descriptors = output_descriptors_from_cbor_account(data, len, &count);
  output_descriptors_free(descriptors, count);
  free(output_descriptor_from_cbor_account(data, len));
}

// Flip, overwrite, drop, insert or cut bytes, favouring CBOR head bytes.
static size_t mutate(uint8_t *data, size_t len, size_t capacity) {
  static const uint8_t heads[] = {0x00, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x40,
                                  0x58, 0x5b, 0x60, 0x80, 0x9f, 0xa0, 0xbf,
                                  0xd8, 0xd9, 0xf4, 0xf5, 0xff};
  for (uint32_t edits = 1 + below(4); edits > 0 && len > 0; edits--) {
    size_t at = below((uint32_t)len);
    switch (below(6)) {
    case 0:
      data[at] ^= (uint8_t)(1u << below(8));
      break;
    case 1:
      data[at] = (uint8_t)rnd();
      break;
    case 2:
      data[at] = heads[below(sizeof heads)];
      break;
    case 3:
      memmove(data + at, data + at + 1, len - at - 1);
      len--;
      break;
    case 4:
      if (len < capacity) {
        memmove(data + at + 1, data + at, len - at);
        data[at] = (uint8_t)rnd();
        len++;
      }
      break;
    default:
      len = at + 1;
      break;
    }
  }
  return len;
}

static void fuzz_cbor(int rounds) {
  printf("\n=== cbor and descriptors (%d rounds) ===\n", rounds);

  // Seeds: every descriptor as a crypto-output, and all of them as one
  // crypto-account {1: fingerprint, 2: [#6.308(output), ...]}.
  static uint8_t seeds[DESCRIPTOR_COUNT + 1][2048];
  size_t seed_len[DESCRIPTOR_COUNT + 1];
  uint8_t *account = seeds[DESCRIPTOR_COUNT];
  size_t n = 0;
  account[n++] = 0xa2;
  account[n++] = 0x01;
  account[n++] = 0x1a;
  account[n++] = 0x34;
  account[n++] = 0x42;
  account[n++] = 0x19;
  account[n++] = 0x3e;
  account[n++] = 0x02;
  account[n++] = (uint8_t)(0x80 | DESCRIPTOR_COUNT);
  for (size_t i = 0; i < DESCRIPTOR_COUNT; i++) {
    output_data_t *output = output_from_descriptor_string(DESCRIPTORS[i]);
    size_t len = 0;
    uint8_t *cbor = output ? output_to_cbor(output, &len) : NULL;
    if (!cbor || len > sizeof seeds[i] || n + 3 + len > sizeof seeds[i]) {
      FAIL("seed descriptor %zu does not encode", i);
      output_free(output);
      free(cbor);
      return;
    }
    check_output_roundtrips(output, "seed descriptor", DESCRIPTORS[i]);
    memcpy(seeds[i], cbor, len);
    seed_len[i] = len;
    account[n++] = 0xd9;
    account[n++] = 0x01;
    account[n++] = 0x34;
    memcpy(account + n, cbor, len);
    n += len;
    output_free(output);
    free(cbor);
  }
  seed_len[DESCRIPTOR_COUNT] = n;

  uint8_t data[2048 + 8];
  for (int round = 0; round < rounds; round++) {
    size_t len;
    if (below(8) == 0) {
      len = below(48);
      for (size_t i = 0; i < len; i++)
        data[i] = (uint8_t)rnd();
    } else {
      size_t seed = below(DESCRIPTOR_COUNT + 1);
      len = seed_len[seed];
      memcpy(data, seeds[seed], len);
      len = mutate(data, len, sizeof data);
    }
    decode_everything(data, len);
    fuzz_descriptor_text();
  }
}

// --- multi-part streams ------------------------------------------------------

// The promises of ur_decoder.h that hold after every part.
static void check_decoder(ur_decoder_t *dec, ur_decoder_state_t state,
                          float *weighted_high) {
  if ((ur_decoder_get_result(dec) != NULL) != (state == UR_DECODER_OK))
    FAIL("a result exists if and only if the state is OK (state %d)", state);
  if (ur_decoder_get_state(dec) != state)
    FAIL("get_state() disagrees with receive_part()");

  float estimate = ur_decoder_estimated_percent_complete(dec);
  float weighted = ur_decoder_estimated_percent_complete_weighted(dec);
  if (!(estimate >= 0.0f && estimate <= 1.0f) ||
      !(weighted >= 0.0f && weighted <= 1.0f))
    FAIL("progress estimate outside [0, 1]");
  if (weighted < *weighted_high)
    FAIL("weighted progress went backwards");
  *weighted_high = weighted;

  if (ur_decoder_received_parts_count(dec) >
      ur_decoder_expected_part_count(dec))
    FAIL("more fragments received than the message has");
}

static size_t put_uint(uint8_t *out, uint64_t value) {
  if (value < 24) {
    out[0] = (uint8_t)value;
    return 1;
  }
  int bytes = value <= 0xff          ? 1
              : value <= 0xffff      ? 2
              : value <= 0xffffffffu ? 4
                                     : 8;
  out[0] = (uint8_t)(bytes == 1 ? 24 : bytes == 2 ? 25 : bytes == 4 ? 26 : 27);
  for (int i = 0; i < bytes; i++)
    out[1 + i] = (uint8_t)(value >> (8 * (bytes - 1 - i)));
  return 1 + (size_t)bytes;
}

// A frame with a valid CRC and a header of the caller's choosing. It gets
// past bytewords, so its fields are what the decoder has to judge.
static char *hostile_frame(uint64_t seq_num, uint64_t seq_len,
                           uint64_t message_len, uint32_t checksum,
                           size_t data_len) {
  uint8_t cbor[1024];
  size_t n = 0;
  cbor[n++] = below(16) ? 0x85 : (uint8_t)(0x80 + below(8));
  n += put_uint(cbor + n, seq_num);
  n += put_uint(cbor + n, seq_len);
  n += put_uint(cbor + n, message_len);
  n += put_uint(cbor + n, checksum);
  size_t head = put_uint(cbor + n, below(16) ? data_len : data_len + below(99));
  cbor[n] |= 0x40;
  n += head;
  for (size_t i = 0; i < data_len; i++)
    cbor[n++] = (uint8_t)rnd();

  char *words = NULL;
  if (!bytewords_encode(cbor, n, &words))
    return NULL;
  size_t size = strlen(words) + 64;
  char *frame = malloc(size);
  if (frame)
    snprintf(frame, size, "ur:%s/%llu-%llu/%s",
             below(10) ? "bytes" : "crypto-psbt",
             (unsigned long long)(below(8) ? seq_num : below(9)),
             (unsigned long long)(below(8) ? seq_len : below(9)), words);
  free(words);
  return frame;
}

// One message through a channel that loses, repeats and delays frames. With
// `hostile`, well-formed frames that are not part of the message are mixed in.
// They carry another checksum, so the message itself must still come out right
// if it comes out at all; without them it must always come out.
static void fuzz_session(bool hostile) {
  static const uint64_t edge[] = {
      0, 1, 2, 1023, 1024, 1025, 262144, 262145, 0xffffffffu, 0x100000000ull};
  static uint8_t message[20000];
  size_t message_len = 10 + below(below(6) ? 2000 : sizeof message - 10);
  for (size_t i = 0; i < message_len; i++)
    message[i] = (uint8_t)rnd();
  size_t max_fragment = 10 + below(below(4) ? 100 : 500);

  if (!ur_encoder_within_limits(message_len, max_fragment, 10))
    return;
  ur_encoder_t *enc =
      ur_encoder_new("bytes", message, message_len, max_fragment,
                     below(4) ? 0 : below(3000), 10);
  ur_decoder_t *dec = ur_decoder_new();
  if (!enc || !dec) {
    FAIL("could not create an encoder and a decoder");
    ur_encoder_free(enc);
    ur_decoder_free(dec);
    return;
  }

  size_t seq_len = ur_encoder_seq_len(enc);
  size_t fragment_len = message_len / seq_len + (message_len % seq_len ? 1 : 0);
  uint32_t loss = below(4) ? below(70) : 0; // percent
  ur_decoder_state_t state = UR_DECODER_PROCESSING;
  float weighted_high = 0.0f;
  char *delayed = NULL;

  for (size_t sent = 0;
       sent < seq_len * 12 + 400 && !ur_decoder_state_is_terminal(state);
       sent++) {
    if (hostile && below(6) == 0) {
      uint64_t h_seq_len = below(3) ? seq_len : edge[below(10)];
      uint64_t h_message_len = below(3) ? message_len : edge[below(10)];
      char *frame = hostile_frame(below(3) ? 1 + below((uint32_t)seq_len * 3)
                                           : edge[below(10)],
                                  h_seq_len, h_message_len, 0x5eed5eedu,
                                  below(3) ? fragment_len : below(600));
      if (frame) {
        state = ur_decoder_receive_part(dec, frame);
        check_decoder(dec, state, &weighted_high);
        free(frame);
      }
      if (ur_decoder_state_is_terminal(state))
        break;
    }

    char *part = NULL;
    if (!ur_encoder_next_part(enc, &part)) {
      FAIL("encoder stopped producing parts");
      break;
    }
    if (below(100) < loss) {
      free(part);
      continue;
    }
    if (!delayed && below(5) == 0) {
      delayed = part; // arrives later, out of order
      continue;
    }
    for (uint32_t copies = below(6) ? 1 : 2;
         copies > 0 && !ur_decoder_state_is_terminal(state); copies--) {
      state = ur_decoder_receive_part(dec, part);
      check_decoder(dec, state, &weighted_high);
    }
    free(part);
    if (delayed && below(3) == 0 && !ur_decoder_state_is_terminal(state)) {
      state = ur_decoder_receive_part(dec, delayed);
      check_decoder(dec, state, &weighted_high);
      free(delayed);
      delayed = NULL;
    }
  }
  free(delayed);

  if (ur_decoder_state_is_terminal(state) &&
      ur_decoder_receive_part(dec, "ur:bytes/aeadaoax") != state)
    FAIL("a terminal state did not stick");

  if (state == UR_DECODER_OK) {
    ur_result_t *result = ur_decoder_get_result(dec);
    if (!result || result->cbor_len != message_len ||
        memcmp(result->cbor_data, message, message_len) != 0)
      FAIL("decoded a different message (%zu bytes, %zu parts)", message_len,
           seq_len);
  } else if (!hostile) {
    FAIL("clean stream did not decode: state %d, %zu bytes, %zu parts, "
         "%u%% loss",
         state, message_len, seq_len, loss);
  }

  ur_decoder_free(dec);
  ur_encoder_free(enc);
}

int main(int argc, char *argv[]) {
  rng_state = argc > 1 ? strtoull(argv[1], NULL, 0) : 0;
  if (rng_state == 0)
    rng_state = 0x9E3779B97F4A7C15ull;
  int rounds = argc > 2 ? atoi(argv[2]) : 1;

  printf("=== UR Fuzz Tests ===\n");
  fuzz_cbor(20000 * rounds);

  int sessions = 300 * rounds;
  printf("\n=== multi-part sessions (%d) ===\n", sessions);
  for (int i = 0; i < sessions; i++)
    fuzz_session(i % 3 == 0);

  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/1\n", failures == 0 ? 1 : 0);
  return failures == 0 ? 0 : 1;
}
