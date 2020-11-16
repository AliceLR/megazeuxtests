/**
 * Copyright (C) 2020 Lachesis <petrifiedrowan@gmail.com>
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

#ifndef MZXTEST_COMMON_HPP
#define MZXTEST_COMMON_HPP

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define O_(...) do { \
  fprintf(stderr, ": " __VA_ARGS__); \
  fflush(stderr); \
} while(0)

template<class T>
constexpr T MAX(T a, T b)
{
  return a > b ? a : b;
}

template<class T>
constexpr T MIN(T a, T b)
{
  return a < b ? a : b;
}

template<class T, int N>
constexpr int arraysize(T (&arr)[N])
{
  return N;
}

template<int N>
static char *fgets_safe(char (&buffer)[N], FILE *fp)
{
  char *retval = fgets(buffer, N, fp);
  if(!retval)
    return NULL;

  size_t len = strlen(buffer);
  while(len && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n'))
    buffer[--len] = '\0';

  return retval;
}

enum class Endian
{
  LITTLE,
  BIG
};

static inline constexpr bool is_big_endian()
{
  const uint32_t t = 0x12345678;
  return *reinterpret_cast<const uint8_t *>(&t) == 0x12;
}

static inline void fix_u16le(uint16_t &value)
{
  if(is_big_endian())
    value = __builtin_bswap16(value);
}

static inline void fix_u32le(uint32_t &value)
{
  if(is_big_endian())
    value = __builtin_bswap32(value);
}

static inline void fix_u16be(uint16_t &value)
{
  if(!is_big_endian())
    value = __builtin_bswap16(value);
}

static inline uint16_t fget_u16le(FILE *fp)
{
  int a = fgetc(fp);
  int b = fgetc(fp);
  return (a) | (b << 8);
}

static inline uint16_t fget_u16be(FILE *fp)
{
  int a = fgetc(fp);
  int b = fgetc(fp);
  return (a << 8) | (b);
}

static inline int16_t fget_s16be(FILE *fp)
{
  int a = fgetc(fp);
  int b = fgetc(fp);
  return (a << 8) | (b);
}

static inline uint32_t fget_u32le(FILE *fp)
{
  int a = fgetc(fp);
  int b = fgetc(fp);
  int c = fgetc(fp);
  int d = fgetc(fp);
  return (a) | (b << 8) | (c << 16) | (d << 24);
}

static inline uint32_t fget_u32be(FILE *fp)
{
  int a = fgetc(fp);
  int b = fgetc(fp);
  int c = fgetc(fp);
  int d = fgetc(fp);
  return (a << 24) | (b << 16) | (c << 8) | (d);
}

static inline bool strip_module_name(char *dest, size_t dest_len)
{
  size_t start = 0;
  size_t end = strlen(dest);

  if(end > dest_len)
    return false;

  // Strip non-ASCII chars and whitespace from the start.
  for(; start < end; start++)
    if(dest[start] >= 0x21 && dest[start] <= 0x7E)
      break;

  // Strip non-ASCII chars and whitespace from the end.
  for(; start < end; end--)
    if(dest[end - 1] >= 0x21 && dest[end - 1] < 0x7E)
      break;

  // Move the buffer to the start of the string, stripping non-ASCII
  // chars and combining spaces.
  size_t i = 0;
  size_t j = start;
  while(i < dest_len - 1 && j < end)
  {
    if(dest[j] == ' ')
    {
      while(dest[j] == ' ')
        j++;
      dest[i++] = ' ';
    }
    else

    if(dest[j] >= 0x21 && dest[j] <= 0x7E)
      dest[i++] = dest[j++];

    else
      j++;
  }
  dest[i] = '\0';
  return true;
}

#endif /* MZXTEST_COMMON_HPP */
