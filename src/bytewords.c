//
// bytewords.c
//
// Copyright © 2025 Krux Contributors
// Licensed under the "BSD-2-Clause Plus Patent License"
//
// Implementation of Bytewords encoding/decoding as specified in:
// https://github.com/BlockchainCommons/Research/blob/master/papers/bcr-2020-012-bytewords.md
//
// This is an independent implementation written using
// foundation-ur-py as a reference for testing and validation.
//

#include "bytewords.h"
#include "crc32.h"
#include "utils.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Complete bytewords table - all 256 words concatenated
static const char bytewords[] =
    "ableacidalsoapexaquaarchatomauntawayaxisbackbaldbarnbeltbetabiasbluebodybr"
    "agbrewbulbbuzzcalmcashcatschefcityclawcodecolacookcostcruxcurlcuspcyandark"
    "datadaysdelidicedietdoordowndrawdropdrumdulldutyeacheasyechoedgeepicevenex"
    "amexiteyesfactfairfernfigsfilmfishfizzflapflewfluxfoxyfreefrogfuelfundgala"
    "gamegeargemsgiftgirlglowgoodgraygrimgurugushgyrohalfhanghardhawkheathelphi"
    "ghhillholyhopehornhutsicedideaidleinchinkyintoirisironitemjadejazzjoinjolt"
    "jowljudojugsjumpjunkjurykeepkenokeptkeyskickkilnkingkitekiwiknoblamblavala"
    "zyleaflegsliarlimplionlistlogoloudloveluaulucklungmainmanymathmazememomenu"
    "meowmildmintmissmonknailnavyneednewsnextnoonnotenumbobeyoboeomitonyxopenov"
    "alowlspaidpartpeckplaypluspoempoolposepuffpumapurrquadquizraceramprealredo"
    "richroadrockroofrubyruinrunsrustsafesagascarsetssilkskewslotsoapsolosongst"
    "ubsurfswantacotasktaxitenttiedtimetinytoiltombtoystriptunatwinuglyundounit"
    "urgeuservastveryvetovialvibeviewvisavoidvowswallwandwarmwaspwavewaxywebswh"
    "atwhenwhizwolfworkyankyawnyellyogayurtzapszerozestzinczonezoom";

// Optimized lookup table for fast word-to-byte mapping
// Uses first and last character positions to create 2D hash
static const int16_t lookup_table[] = {
    4,   14,  29,  37,  -1,  -1,  73,  -1,  99,  -1,  -1,  128, -1,  -1,  -1,
    177, -1,  -1,  194, 217, -1,  230, -1,  -1,  248, -1,  -1,  20,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  126, 127, -1,  160, -1,  -1,  -1,  -1,  203,
    214, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  53,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  253, 1,   11,  -1,  -1,  -1,  72,  80,  88,  98,  -1,  -1,  137,
    149, 155, -1,  168, 179, 186, -1,  210, -1,  231, 234, -1,  -1,  -1,  0,
    16,  28,  40,  52,  69,  74,  95,  100, 107, 124, 138, 145, 159, 162, 175,
    -1,  181, 193, 211, 222, 228, 237, -1,  -1,  254, -1,  -1,  25,  -1,  -1,
    -1,  -1,  86,  -1,  -1,  -1,  130, -1,  -1,  -1,  176, -1,  188, 204, -1,
    -1,  -1,  243, -1,  -1,  -1,  -1,  18,  -1,  -1,  -1,  70,  -1,  87,  -1,
    -1,  123, 141, -1,  -1,  -1,  -1,  -1,  -1,  202, -1,  -1,  -1,  -1,  -1,
    -1,  -1,  5,   -1,  23,  -1,  49,  63,  84,  92,  101, -1,  -1,  -1,  144,
    -1,  -1,  -1,  -1,  185, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  39,  -1,  -1,  -1,  -1,  -1,  -1,  125, -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  208, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  10,  30,  36,  -1,  -1,  -1,  89,  -1,  115,
    121, 140, 152, -1,  -1,  170, -1,  187, 197, 207, -1,  -1,  244, -1,  245,
    -1,  -1,  -1,  33,  47,  -1,  71,  78,  93,  -1,  111, -1,  -1,  -1,  153,
    166, 174, -1,  183, -1,  213, -1,  227, 233, -1,  247, -1,  6,   -1,  22,
    46,  55,  62,  82,  -1,  106, -1,  -1,  -1,  -1,  -1,  -1,  173, -1,  -1,
    -1,  -1,  -1,  -1,  235, -1,  -1,  255, -1,  12,  35,  43,  54,  60,  -1,
    96,  105, 109, 122, 134, 142, 158, 165, -1,  -1,  190, 205, 218, -1,  -1,
    241, -1,  246, -1,  2,   -1,  -1,  -1,  51,  -1,  85,  -1,  103, 112, 118,
    136, 146, -1,  -1,  -1,  -1,  184, 201, 206, 220, 226, -1,  -1,  -1,  251,
    -1,  -1,  34,  45,  -1,  65,  -1,  91,  -1,  114, 117, 133, -1,  -1,  -1,
    -1,  -1,  182, 200, 216, -1,  -1,  236, -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  42,  -1,  59,  75,  -1,
    -1,  -1,  -1,  132, -1,  -1,  -1,  178, -1,  -1,  195, -1,  223, -1,  -1,
    -1,  -1,  -1,  9,   15,  24,  38,  57,  61,  76,  97,  104, 113, 120, 131,
    151, 156, 167, 172, -1,  191, 196, 215, -1,  232, 239, -1,  -1,  250, 7,
    13,  31,  41,  56,  58,  77,  90,  -1,  110, 119, 135, 150, 157, 163, 169,
    -1,  192, 199, 209, 221, 224, 240, -1,  249, 252, -1,  -1,  -1,  -1,  -1,
    -1,  83,  -1,  -1,  -1,  -1,  139, 147, -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  19,  27,  44,  -1,  66,  79,  -1,  -1,  -1,  -1,  -1,  148,
    -1,  -1,  -1,  -1,  -1,  198, -1,  -1,  229, -1,  -1,  -1,  -1,  3,   -1,
    32,  -1,  -1,  67,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  164, -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  8,   17,  26,  48,  50,  68,
    81,  94,  102, 116, -1,  129, 143, 154, 161, 171, -1,  189, -1,  212, 219,
    225, 238, -1,  -1,  -1,  -1,  21,  -1,  -1,  -1,  64,  -1,  -1,  -1,  108,
    -1,  -1,  -1,  -1,  -1,  -1,  180, -1,  -1,  -1,  -1,  -1,  242, -1,  -1,
    -1};

// Helper function to count occurrences of a character in a string
static size_t count_char_occurrences(const char *str, char ch) {
  if (!str)
    return 0;

  size_t count = 0;
  const char *ptr = str;
  while (*ptr) {
    if (*ptr == ch) {
      count++;
    }
    ptr++;
  }
  return count;
}

// Helper function to calculate dynamic word count with safety margin
static size_t calculate_word_count_with_margin(const char *encoded,
                                               size_t word_len,
                                               char separator) {
  if (!encoded)
    return 0;

  size_t estimated_words;
  if (word_len == 4) {
    estimated_words = count_char_occurrences(encoded, separator) + 1;
  } else {
    estimated_words = strlen(encoded) / word_len;
  }

  size_t margin = estimated_words / 20; // 5% margin
  if (margin < 10)
    margin = 10;

  return estimated_words + margin;
}

// Optimized word decoding using lookup table
static bool decode_word_optimized(const char *word, size_t word_len,
                                  uint8_t *output) {
  if (word_len != 2 && word_len != 4) {
    return false;
  }

  int x = tolower(word[0]) - 'a';
  int y = tolower(word[word_len == 4 ? 3 : 1]) - 'a';

  if (x < 0 || x >= 26 || y < 0 || y >= 26) {
    return false;
  }

  size_t offset = y * 26 + x;
  int16_t value = lookup_table[offset];
  if (value == -1) {
    return false;
  }

  if (word_len == 4) {
    const char *byteword = bytewords + value * 4;
    if (tolower(word[1]) != byteword[1] || tolower(word[2]) != byteword[2]) {
      return false;
    }
  }

  *output = (uint8_t)value;
  return true;
}

// Split string into words of fixed length (for minimal style)
static size_t partition_string(const char *str, size_t word_len, char ***parts,
                               size_t max_parts) {
  size_t str_len = strlen(str);
  size_t word_count = str_len / word_len;

  if (word_count > max_parts) {
    word_count = max_parts;
  }

  *parts = safe_malloc(sizeof(char *) * word_count);
  if (!*parts)
    return 0;

  for (size_t i = 0; i < word_count; i++) {
    (*parts)[i] = safe_malloc(word_len + 1);
    if (!(*parts)[i]) {
      for (size_t j = 0; j < i; j++) {
        free((*parts)[j]);
      }
      free(*parts);
      return 0;
    }
    strncpy((*parts)[i], str + i * word_len, word_len);
    (*parts)[i][word_len] = '\0';
  }

  return word_count;
}

bool bytewords_decode(bytewords_style_t style, const char *encoded,
                      uint8_t **decoded, size_t *decoded_len) {
  if (!encoded || !decoded || !decoded_len)
    return false;

  size_t word_len = (style == BYTEWORDS_STYLE_MINIMAL) ? 2 : 4;
  char separator = (style == BYTEWORDS_STYLE_STANDARD) ? ' '
                   : (style == BYTEWORDS_STYLE_URI)    ? '-'
                                                       : 0;

  // Split into words
  char **words;
  size_t num_words;

  // Calculate dynamic allocation size with safety margin
  size_t max_words =
      calculate_word_count_with_margin(encoded, word_len, separator);

  if (word_len == 4) {
    // Use separator-based splitting
    words = safe_malloc(sizeof(char *) * max_words);
    if (!words)
      return false;
    num_words = str_split(encoded, separator, words, max_words);
  } else {
    // Use fixed-length partitioning for minimal style
    num_words = partition_string(encoded, word_len, &words, max_words);
  }

  if (num_words < 5) {
    free_string_array(words, num_words);
    free(words);
    return false;
  }

  uint8_t *buf = safe_malloc(num_words);
  if (!buf) {
    free_string_array(words, num_words);
    free(words);
    return false;
  }

  for (size_t i = 0; i < num_words; i++) {
    if (!decode_word_optimized(words[i], word_len, &buf[i])) {
      free(buf);
      free_string_array(words, num_words);
      free(words);
      return false;
    }
  }

  if (num_words < 4) {
    free(buf);
    free_string_array(words, num_words);
    free(words);
    return false;
  }

  size_t body_size = num_words - 4;
  uint8_t *body = safe_malloc(body_size);
  if (!body) {
    free(buf);
    free_string_array(words, num_words);
    free(words);
    return false;
  }

  memcpy(body, buf, body_size);

  uint32_t expected_crc = crc32_calculate(body, body_size);
  uint32_t received_crc = (buf[body_size] << 24) | (buf[body_size + 1] << 16) |
                          (buf[body_size + 2] << 8) | buf[body_size + 3];

  if (expected_crc != received_crc) {
    free(body);
    free(buf);
    free_string_array(words, num_words);
    free(words);
    return false;
  }

  *decoded = body;
  *decoded_len = body_size;

  free(buf);
  free_string_array(words, num_words);
  free(words);
  return true;
}

bool bytewords_encode(bytewords_style_t style, const uint8_t *data,
                      size_t data_len, char **encoded) {
  if (!data || !encoded || data_len == 0)
    return false;

  uint32_t crc = crc32_calculate(data, data_len);
  size_t total_len = data_len + 4;
  uint8_t *buf = safe_malloc(total_len);
  if (!buf)
    return false;

  memcpy(buf, data, data_len);
  buf[data_len] = (crc >> 24) & 0xFF;
  buf[data_len + 1] = (crc >> 16) & 0xFF;
  buf[data_len + 2] = (crc >> 8) & 0xFF;
  buf[data_len + 3] = crc & 0xFF;

  if (style == BYTEWORDS_STYLE_MINIMAL) {
    *encoded = safe_malloc(total_len * 2 + 1);
    if (!*encoded) {
      free(buf);
      return false;
    }

    char *pos = *encoded;
    for (size_t i = 0; i < total_len; i++) {
      const char *word = bytewords + buf[i] * 4;
      *pos++ = word[0];
      *pos++ = word[3];
    }
    *pos = '\0';
  } else {
    char separator = (style == BYTEWORDS_STYLE_STANDARD) ? ' ' : '-';
    size_t encoded_len = total_len * 4 + (total_len - 1) + 1;
    *encoded = safe_malloc(encoded_len);
    if (!*encoded) {
      free(buf);
      return false;
    }

    char *pos = *encoded;
    for (size_t i = 0; i < total_len; i++) {
      const char *word = bytewords + buf[i] * 4;
      strncpy(pos, word, 4);
      pos += 4;
      if (i < total_len - 1) {
        *pos++ = separator;
      }
    }
    *pos = '\0';
  }

  free(buf);
  return true;
}

void bytewords_free(void *ptr) {
  if (ptr) {
    free(ptr);
  }
}

bool bytewords_decode_raw(bytewords_style_t style, const char *encoded,
                          uint8_t **decoded, size_t *decoded_len) {
  if (!encoded || !decoded || !decoded_len)
    return false;

  size_t word_len = (style == BYTEWORDS_STYLE_MINIMAL) ? 2 : 4;
  char separator = (style == BYTEWORDS_STYLE_STANDARD) ? ' '
                   : (style == BYTEWORDS_STYLE_URI)    ? '-'
                                                       : 0;

  // Split into words
  char **words;
  size_t num_words;

  // Calculate dynamic allocation size with safety margin
  size_t max_words =
      calculate_word_count_with_margin(encoded, word_len, separator);

  if (word_len == 4) {
    // Use separator-based splitting
    words = safe_malloc(sizeof(char *) * max_words);
    if (!words)
      return false;
    num_words = str_split(encoded, separator, words, max_words);
  } else {
    // Use fixed-length partitioning for minimal style
    num_words = partition_string(encoded, word_len, &words, max_words);
  }

  if (num_words == 0) {
    free_string_array(words, num_words);
    free(words);
    return false;
  }

  uint8_t *buf = safe_malloc(num_words);
  if (!buf) {
    free_string_array(words, num_words);
    free(words);
    return false;
  }

  for (size_t i = 0; i < num_words; i++) {
    if (!decode_word_optimized(words[i], word_len, &buf[i])) {
      free(buf);
      free_string_array(words, num_words);
      free(words);
      return false;
    }
  }

  if (num_words < 4) {
    free(buf);
    free_string_array(words, num_words);
    free(words);
    return false;
  }

  size_t body_size = num_words - 4;
  uint8_t *body = safe_malloc(body_size);
  if (!body) {
    free(buf);
    free_string_array(words, num_words);
    free(words);
    return false;
  }

  memcpy(body, buf, body_size);

  *decoded = body;
  *decoded_len = body_size;

  free(buf);
  free_string_array(words, num_words);
  free(words);
  return true;
}

bool bytewords_encode_raw(bytewords_style_t style, const uint8_t *data,
                          size_t data_len, char **encoded) {
  if (!data || !encoded || data_len == 0)
    return false;

  if (style == BYTEWORDS_STYLE_MINIMAL) {
    *encoded = safe_malloc(data_len * 2 + 1);
    if (!*encoded)
      return false;

    char *pos = *encoded;
    for (size_t i = 0; i < data_len; i++) {
      const char *word = bytewords + data[i] * 4;
      *pos++ = word[0];
      *pos++ = word[3];
    }
    *pos = '\0';
  } else {
    char separator = (style == BYTEWORDS_STYLE_STANDARD) ? ' ' : '-';
    size_t encoded_len = data_len * 4 + (data_len - 1) + 1;
    *encoded = safe_malloc(encoded_len);
    if (!*encoded)
      return false;

    char *pos = *encoded;
    for (size_t i = 0; i < data_len; i++) {
      const char *word = bytewords + data[i] * 4;
      strncpy(pos, word, 4);
      pos += 4;
      if (i < data_len - 1) {
        *pos++ = separator;
      }
    }
    *pos = '\0';
  }

  return true;
}
