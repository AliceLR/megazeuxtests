/**
 * Copyright (C) 2021 Lachesis <petrifiedrowan@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdint.h>

#include "encode.hpp"

/**
 * UTF-8 helper functions.
 */

static constexpr size_t utf8_codepoint_length(uint32_t codepoint)
{
  if(codepoint <= 0x7f)
    return 1;
  else

  if(codepoint <= 0x7ff)
    return 2;
  else
  if(codepoint <= 0xffff)
    return 3;
  else

  if(codepoint <= 0x10FFFF)
    return 4;

  return SIZE_MAX;
}

static constexpr size_t utf32_to_utf8(char *&dest, char *dest_end, uint32_t codepoint)
{
  if(codepoint <= 0x7f)
  {
    if(dest > dest_end - 1)
      return 0;

    *dest++ = codepoint;
    return 1;
  }
  else

  if(codepoint <= 0x7ff)
  {
    if(dest > dest_end - 2)
      return 0;

    *dest++ = 0b11000000 | ((codepoint & 0x7c0) >> 6);
    *dest++ = 0b10000000 |  (codepoint & 0x03f);
    return 2;
  }
  else

  if(codepoint <= 0xffff)
  {
    if(dest > dest_end - 3)
      return 0;

    *dest++ = 0b11100000 | ((codepoint & 0xf000) >> 12);
    *dest++ = 0b10000000 | ((codepoint & 0x0fc0) >> 6);
    *dest++ = 0b10000000 |  (codepoint & 0x003f);
    return 3;
  }
  else

  if(codepoint <= 0x10FFFF)
  {
    if(dest > dest_end - 4)
      return 0;

    *dest++ = 0b11110000 | ((codepoint & 0x1c0000) >> 18);
    *dest++ = 0b10000000 | ((codepoint & 0x03f000) >> 12);
    *dest++ = 0b10000000 | ((codepoint & 0x000fc0) >> 6);
    *dest++ = 0b10000000 |  (codepoint & 0x00003f);
    return 4;
  }
  return 0;
}


/**
 * Strip encoding.
 * Replaces all control chars and extended ASCII with '.'.
 */

size_t encode::strip::utf8_count(const char *in, size_t in_len)
{
  return in_len;
}

ssize_t encode::strip::utf8_encode(char *out, size_t out_len, const char *in, size_t in_len)
{
  if(out_len < in_len)
    return -1;

  for(size_t i = 0; i < in_len; i++)
  {
    uint32_t codepoint = *in++;
    if(codepoint >= 32 && codepoint <= 126)
      *out++ = codepoint;
    else
      *out++ = '.';
  }
  return in_len;
}


/**
 * Code Page 437.
 * This codepage is reasonable to assume as the encoding for most DOS software.
 */

static constexpr uint16_t cp437_to_utf32[256] =
{
  /* CP437 control codes. */
  0x2400, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
  0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,

  /* ASCII and CP437 delete. */
  ' ', '!', '"', '#', '$', '%', '&', '\'', '(', ')', '*', '+', ',',  '-', '.', '/',
  '0', '1', '2', '3', '4', '5', '6', '7',  '8', '9', ':', ';', '<',  '=', '>', '?',
  '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',  'H', 'I', 'J', 'K', 'L',  'M', 'N', 'O',
  'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',  'X', 'Y', 'Z', '[', '\\', ']', '^', '_',
  '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g',  'h', 'i', 'j', 'k', 'l',  'm', 'n', 'o',
  'p', 'q', 'r', 's', 't', 'u', 'v', 'w',  'x', 'y', 'z', '{', '|',  '}', '~', 0x2302,

  /* CP437 extended. */
  0xc7,   0xfc,   0xe9,   0xe2,   0xe4,   0xe0,   0xe5,   0xe7,   0xea,   0xeb,   0xe8,   0xef,   0xee,   0xec,   0xc4,   0xc5,
  0xc9,   0xe6,   0xc6,   0xf4,   0xf6,   0xf2,   0xfb,   0xf9,   0xff,   0xd6,   0xdc,   0xa2,   0xa3,   0xa5,   0x20a7, 0x192,
  0xe1,   0xed,   0xf3,   0xfa,   0xf1,   0xd1,   0xaa,   0xba,   0xbf,   0x2310, 0xac,   0xbd,   0xbc,   0xa1,   0xab,   0xbb,
  0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
  0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
  0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, 0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
  0x3b1,  0xdf,   0x393,  0x3c0,  0x3a3,  0x3c3,  0xb5,   0x3c4,  0x3a6,  0x398,  0x3a9,  0x3b4,  0x221e, 0x3c6,  0x3b5,  0x2229,
  0x2261, 0xb1,   0x2265, 0x2264, 0x2320, 0x2321, 0xf7,   0x2248, 0xb0,   0x2219, 0xb7,   0x221a, 0x207f, 0xb2,   0x25a0, 0xa0,
};

size_t encode::cp437::utf8_count(const char *in, size_t in_len)
{
  size_t count = 0;

  for(size_t i = 0; i < in_len; i++)
  {
    uint32_t codepoint = cp437_to_utf32[(uint8_t)*in++];
    count += utf8_codepoint_length(codepoint);
  }
  return count;
};

ssize_t encode::cp437::utf8_encode(char *out, size_t out_len, const char *in, size_t in_len)
{
  char *out_end = out + out_len;
  if(out_end < out)
    return -1;

  size_t count = 0;

  for(size_t i = 0; i < in_len; i++)
  {
    uint32_t codepoint = cp437_to_utf32[(uint8_t)*in++];
    size_t len = utf32_to_utf8(out, out_end, codepoint);
    if(len < 1)
      return -1;

    count += len;
  }
  return count;
};
