//
// fountain_utils.c
//
// Copyright © 2025 Krux Contributors
// Licensed under the "BSD-2-Clause Plus Patent License"
//
// Utility functions for fountain code operations.
//
// This is an independent implementation written using
// foundation-ur-py as a reference for testing and validation.
//

#include "fountain_utils.h"
#include <mbedtls/sha256.h>
#include "utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void compute_sha256(const uint8_t *input, size_t len,
                           uint8_t output[32]) {
  // Use ESP32P4 hardware-accelerated SHA256 via mbedtls
  mbedtls_sha256(input, len, output, 0);  // 0 = SHA256 (not SHA224)
}

static int compare_size_t(const void *a, const void *b) {
  size_t arg1 = *(const size_t *)a;
  size_t arg2 = *(const size_t *)b;
  if (arg1 < arg2)
    return -1;
  if (arg1 > arg2)
    return 1;
  return 0;
}

static uint64_t rotl(const uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

void prng_init_from_bytes(prng_state_t *prng, const uint8_t *seed,
                          size_t seed_len) {
  if (!prng || !seed)
    return;

  uint8_t hash[32];
  compute_sha256(seed, seed_len, hash);

  for (int i = 0; i < 4; i++) {
    prng->state[i] = 0;
    for (int j = 0; j < 8; j++) {
      prng->state[i] <<= 8;
      prng->state[i] |= hash[i * 8 + j];
    }
  }
}

static uint64_t prng_next_uint64(prng_state_t *prng) {
  const uint64_t result = rotl(prng->state[1] * 5, 7) * 9;
  const uint64_t t = prng->state[1] << 17;

  prng->state[2] ^= prng->state[0];
  prng->state[3] ^= prng->state[1];
  prng->state[1] ^= prng->state[2];
  prng->state[0] ^= prng->state[3];

  prng->state[2] ^= t;
  prng->state[3] = rotl(prng->state[3], 45);

  return result;
}

uint32_t prng_next_int(prng_state_t *prng, uint32_t min, uint32_t max) {
  if (!prng || min > max)
    return min;

  double range = (double)(max - min + 1);
  double rand_val = prng_next_double(prng);
  uint64_t result = (uint64_t)(rand_val * range + min);

  return (uint32_t)(result & 0xFFFFFFFFULL);
}

double prng_next_double(prng_state_t *prng) {
  if (!prng)
    return 0.0;

  uint64_t val = prng_next_uint64(prng);
  return (double)val / (double)(0xFFFFFFFFFFFFFFFFULL + 1.0);
}

bool random_sampler_init(random_sampler_t *sampler, double *probs,
                         size_t count) {
  if (!sampler || !probs || count == 0)
    return false;

  double total = 0.0;
  for (size_t i = 0; i < count; i++) {
    if (probs[i] <= 0.0)
      return false;
    total += probs[i];
  }
  if (total <= 0.0)
    return false;

  sampler->probs = safe_malloc(count * sizeof(double));
  sampler->aliases = safe_malloc(count * sizeof(int));
  if (!sampler->probs || !sampler->aliases) {
    random_sampler_free(sampler);
    return false;
  }
  sampler->count = count;

  double *P = safe_malloc(count * sizeof(double));
  if (!P) {
    random_sampler_free(sampler);
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    P[i] = (probs[i] * count) / total;
  }

  int *small = safe_malloc(count * sizeof(int));
  int *large = safe_malloc(count * sizeof(int));
  if (!small || !large) {
    free(P);
    free(small);
    free(large);
    random_sampler_free(sampler);
    return false;
  }

  size_t small_size = 0, large_size = 0;

  for (int i = (int)count - 1; i >= 0; i--) {
    if (P[i] < 1.0) {
      small[small_size++] = i;
    } else {
      large[large_size++] = i;
    }
  }

  while (small_size > 0 && large_size > 0) {
    int a = small[--small_size];
    int g = large[--large_size];

    sampler->probs[a] = P[a];
    sampler->aliases[a] = g;
    P[g] = P[g] + P[a] - 1.0;

    if (P[g] < 1.0) {
      small[small_size++] = g;
    } else {
      large[large_size++] = g;
    }
  }

  while (large_size > 0) {
    sampler->probs[large[--large_size]] = 1.0;
  }
  while (small_size > 0) {
    sampler->probs[small[--small_size]] = 1.0;
  }

  free(P);
  free(small);
  free(large);
  return true;
}

void random_sampler_free(random_sampler_t *sampler) {
  if (!sampler)
    return;
  free(sampler->probs);
  free(sampler->aliases);
  sampler->probs = NULL;
  sampler->aliases = NULL;
  sampler->count = 0;
}

int random_sampler_next(random_sampler_t *sampler, prng_state_t *rng) {
  if (!sampler || !sampler->probs || !sampler->aliases || sampler->count == 0)
    return 0;

  double r1 = prng_next_double(rng);
  double r2 = prng_next_double(rng);
  int i = (int)((double)sampler->count * r1);

  if (i >= (int)sampler->count)
    i = (int)sampler->count - 1;

  return (r2 < sampler->probs[i]) ? i : sampler->aliases[i];
}

static size_t choose_degree(size_t seq_len, prng_state_t *prng) {
  if (seq_len == 0)
    return 1;

  double *degree_probs = safe_malloc(seq_len * sizeof(double));
  if (!degree_probs)
    return 1;

  for (size_t i = 0; i < seq_len; i++) {
    degree_probs[i] = 1.0 / (i + 1);
  }

  random_sampler_t sampler = {0};
  if (!random_sampler_init(&sampler, degree_probs, seq_len)) {
    free(degree_probs);
    return 1;
  }

  int degree_index = random_sampler_next(&sampler, prng);
  size_t degree = degree_index + 1;

  random_sampler_free(&sampler);
  free(degree_probs);

  return degree;
}

bool choose_fragments(uint32_t seq_num, size_t seq_len, uint32_t checksum,
                      part_indexes_t *result) {
  if (!result || seq_len == 0)
    return false;

  part_indexes_clear(result);

  if (seq_num <= seq_len) {
    return part_indexes_add(result, seq_num - 1);
  }

  uint8_t seed[8];
  seed[0] = (seq_num >> 24) & 0xff;
  seed[1] = (seq_num >> 16) & 0xff;
  seed[2] = (seq_num >> 8) & 0xff;
  seed[3] = seq_num & 0xff;
  seed[4] = (checksum >> 24) & 0xff;
  seed[5] = (checksum >> 16) & 0xff;
  seed[6] = (checksum >> 8) & 0xff;
  seed[7] = checksum & 0xff;

  prng_state_t rng;
  prng_init_from_bytes(&rng, seed, 8);

  size_t degree = choose_degree(seq_len, &rng);

  size_t *shuffled_indexes = safe_malloc(seq_len * sizeof(size_t));
  if (!shuffled_indexes)
    return false;

  size_t *remaining_indexes = safe_malloc(seq_len * sizeof(size_t));
  if (!remaining_indexes) {
    free(shuffled_indexes);
    return false;
  }

  for (size_t i = 0; i < seq_len; i++) {
    remaining_indexes[i] = i;
  }

  size_t remaining_count = seq_len;
  for (size_t i = 0; i < seq_len && remaining_count > 0; i++) {
    uint32_t idx = prng_next_int(&rng, 0, remaining_count - 1);
    shuffled_indexes[i] = remaining_indexes[idx];

    for (size_t j = idx; j < remaining_count - 1; j++) {
      remaining_indexes[j] = remaining_indexes[j + 1];
    }
    remaining_count--;
  }

  for (size_t i = 0; i < degree && i < seq_len; i++) {
    if (!part_indexes_add(result, shuffled_indexes[i])) {
      free(shuffled_indexes);
      free(remaining_indexes);
      return false;
    }
  }

  free(shuffled_indexes);
  free(remaining_indexes);
  return true;
}

bool part_indexes_is_strict_subset(const part_indexes_t *a,
                                   const part_indexes_t *b) {
  if (!a || !b)
    return false;

  if (a->count >= b->count)
    return false;

  for (size_t i = 0; i < a->count; i++) {
    if (!part_indexes_contains(b, a->indexes[i])) {
      return false;
    }
  }

  return true;
}

bool part_indexes_difference(const part_indexes_t *a, const part_indexes_t *b,
                             part_indexes_t *result) {
  if (!a || !b || !result)
    return false;

  part_indexes_clear(result);

  for (size_t i = 0; i < a->count; i++) {
    if (!part_indexes_contains(b, a->indexes[i])) {
      if (!part_indexes_add(result, a->indexes[i])) {
        return false;
      }
    }
  }

  qsort(result->indexes, result->count, sizeof(size_t), compare_size_t);
  return true;
}

bool part_indexes_equal(const part_indexes_t *a, const part_indexes_t *b) {
  if (!a || !b)
    return false;

  if (a->count != b->count)
    return false;

  for (size_t i = 0; i < a->count; i++) {
    if (!part_indexes_contains(b, a->indexes[i])) {
      return false;
    }
  }

  return true;
}

bool part_indexes_copy(const part_indexes_t *src, part_indexes_t *dst) {
  if (!src || !dst)
    return false;

  part_indexes_clear(dst);

  for (size_t i = 0; i < src->count; i++) {
    if (!part_indexes_add(dst, src->indexes[i])) {
      return false;
    }
  }

  return true;
}

bool join_fragments(uint8_t **fragments, size_t *fragment_lens,
                    size_t fragment_count, size_t message_len,
                    uint8_t *result) {
  if (!fragments || !fragment_lens || !result || fragment_count == 0)
    return false;

  size_t total_len = 0;
  for (size_t i = 0; i < fragment_count; i++) {
    if (fragments[i] && fragment_lens[i] > 0) {
      total_len += fragment_lens[i];
    }
  }

  uint8_t *temp_buffer = safe_malloc(total_len);
  if (!temp_buffer)
    return false;

  size_t offset = 0;
  for (size_t i = 0; i < fragment_count; i++) {
    if (fragments[i] && fragment_lens[i] > 0) {
      memcpy(temp_buffer + offset, fragments[i], fragment_lens[i]);
      offset += fragment_lens[i];
    }
  }

  size_t copy_len = (total_len < message_len) ? total_len : message_len;
  memcpy(result, temp_buffer, copy_len);

  free(temp_buffer);
  return true;
}

bool part_indexes_have_intersection(const part_indexes_t *a,
                                    const part_indexes_t *b) {
  if (!a || !b)
    return false;

  for (size_t i = 0; i < a->count; i++) {
    if (part_indexes_contains(b, a->indexes[i])) {
      return true;
    }
  }
  return false;
}

bool part_indexes_symmetric_difference(const part_indexes_t *a,
                                       const part_indexes_t *b,
                                       part_indexes_t *result) {
  if (!a || !b || !result)
    return false;

  part_indexes_clear(result);

  for (size_t i = 0; i < a->count; i++) {
    if (!part_indexes_contains(b, a->indexes[i])) {
      if (!part_indexes_add(result, a->indexes[i])) {
        return false;
      }
    }
  }

  for (size_t i = 0; i < b->count; i++) {
    if (!part_indexes_contains(a, b->indexes[i])) {
      if (!part_indexes_add(result, b->indexes[i])) {
        return false;
      }
    }
  }

  return true;
}