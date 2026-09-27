/*
 * test_ur_fountain_utils.c
 *
 * Fragment selection must match the reference implementations bit for bit:
 * multipart frames carry no index list, so every decoder re-derives it.
 */

#include "../src/fountain_utils.h"
#include <stdio.h>

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

static void test_degree_sampler_matches_reference(void) {
  printf("\n=== degree_sampler_matches_reference ===\n");

  // Alias table for the degree weights 1/1 .. 1/5, as built by bc-ur's
  // RandomSampler (P[g] += P[a] - 1).
  static const double expected_probs[5] = {
      0x1.cf6a82cd8c255p-1, 0x1.0000000000000p+0, 0x1.75b8fe21a291cp-1,
      0x1.184abe9939ed5p-1, 0x1.c077975b8fe22p-2};
  static const int expected_aliases[5] = {1, 0, 0, 0, 0};

  double weights[5];
  for (int i = 0; i < 5; i++)
    weights[i] = 1.0 / (i + 1);

  random_sampler_t sampler = {0};
  ASSERT(random_sampler_init(&sampler, weights, 5), "sampler builds");

  int probs_match = 1, aliases_match = 1;
  for (int i = 0; i < 5 && sampler.probs; i++) {
    probs_match &= sampler.probs[i] == expected_probs[i];
    aliases_match &= sampler.aliases[i] == expected_aliases[i];
  }
  ASSERT(sampler.probs && probs_match, "probabilities match the reference");
  ASSERT(sampler.aliases && aliases_match, "aliases match the reference");
  random_sampler_free(&sampler);
}

int main(void) {
  printf("=== Fountain Utils Tests ===\n");
  test_degree_sampler_matches_reference();
  printf("\n=== Summary ===\n");
  printf("Tests passed: %d/%d\n", asserts - failures, asserts);
  return failures == 0 ? 0 : 1;
}
