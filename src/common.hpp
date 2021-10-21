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
#include <sys/stat.h>

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

static inline long get_file_length(FILE *fp)
{
  struct stat st;
  int fd = fileno(fp);
  long ret;

  if(fd >= 0)
  {
#ifdef _WIN32
    ret = _filelength(fd);
    if(ret >= 0)
      return ret;
#endif
    ret = fstat(fd, &st);
    if(ret == 0)
      return st.st_size;
  }
  long pos = ftell(fp);
  if(pos >= 0 && !fseek(fp, 0, SEEK_END))
  {
    ret = ftell(fp);
    fseek(fp, pos, SEEK_SET);
    return ret;
  }
  return -1;
}

enum class Endian
{
  LITTLE,
  BIG
};

/* Multibyte memory reading functions. */

static inline constexpr uint16_t mem_u16le(const void *_mem)
{
  // this makes clang unhappy :-(
  //const uint8_t *mem = reinterpret_cast<const uint8_t *>(_mem);
  const uint8_t *mem = (const uint8_t *)_mem;
  return mem[0] | (mem[1] << 8);
}

static inline constexpr uint16_t mem_u16be(const void *_mem)
{
  const uint8_t *mem = (const uint8_t *)_mem;
  return (mem[0] << 8) | mem[1];
}

static inline constexpr int16_t mem_s16le(const void *mem)
{
  return mem_u16le(mem);
}

static inline constexpr int16_t mem_s16be(const void *mem)
{
  return mem_u16be(mem);
}

static inline constexpr uint32_t mem_u32le(const void *_mem)
{
  const uint8_t *mem = (const uint8_t *)_mem;
  return mem[0] | (mem[1] << 8) | (mem[2] << 16) | (mem[3] << 24);
}

static inline constexpr uint32_t mem_u32be(const void *_mem)
{
  const uint8_t *mem = (const uint8_t *)_mem;
  return (mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | mem[3];
}

/* Multibyte file reading functions. */

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

static inline int16_t fget_s16le(FILE *fp)
{
  int a = fgetc(fp);
  int b = fgetc(fp);
  return (a) | (b << 8);
}

static inline int16_t fget_s16be(FILE *fp)
{
  int a = fgetc(fp);
  int b = fgetc(fp);
  return (a << 8) | (b);
}

static inline uint32_t fget_u24le(FILE *fp)
{
  int a = fgetc(fp);
  int b = fgetc(fp);
  int c = fgetc(fp);
  return (a) | (b << 8) | (c << 16);
}

static inline uint32_t fget_u24be(FILE *fp)
{
  int a = fgetc(fp);
  int b = fgetc(fp);
  int c = fgetc(fp);
  return (a << 16) | (b << 8) | (c);
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

/* String cleaning functions. */

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

/* Path functions. */

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

static bool isslash(char c)
{
  return c == '/' || c == '\\';
}

static inline char *path_tokenize(char **cursor)
{
  char *tok = *cursor;
  if(tok)
  {
    char *sep = strpbrk(tok, "/\\");
    if(sep)
      *(sep++) = '\0';

    *cursor = sep;
  }
  return tok;
}

static inline size_t path_clean_slashes(char *path)
{
  char *current;
  size_t len = 0;

  for(current = path; *current; current++, len++)
  {
    if(isslash(*current))
    {
      *current = DIR_SEPARATOR;
      if(isslash(current[1]))
        break;
    }
  }

  // Rewrite loop (only required if multiple consecutive slashes exist).
  while(*current)
  {
    if(isslash(*current))
    {
      while(isslash(*current))
        current++;

      path[len++] = DIR_SEPARATOR;
    }
    else
      path[len++] = *(current++);
  }
  path[len] = '\0';
  return len;
}

/* Date functions. */

/* Utility function: get the number of days since the extended
 * Gregorian date 0000-03-01. Useful for conversion of dates
 * defined in "number of [days,seconds] since [epoch]". */
static inline constexpr uint64_t date_to_total_days(int year, int month, int day)
{
  uint64_t m = (month + 9) % 12;
  uint64_t y = year - m / 10;
  y = 365 * y + (y / 4) - (y / 100) + (y / 400);
  m = (m * 306 + 5) / 10;

  return y + m + (day - 1);
}

/**
 * Utility function: convert a number days since the extended Gregorian
 * date 0000-03-01 to a real date.
 */
static inline void total_days_to_date(uint64_t total_days, int *year, int *month, int *day)
{
  int y = (10000 * total_days + 14780) / 3652425;
  int64_t dayofyear = total_days - (365 * y + (y / 4) - (y / 100) + (y / 400));
  if(dayofyear < 0)
  {
    y--;
    dayofyear = total_days - (365 * y + (y / 4) - (y / 100) + (y / 400));
  }
  int m = (100 * dayofyear + 52) / 3060;
  int d = dayofyear - (m * 306 + 5) / 10 + 1;

  y += (m + 2) / 12;
  m = (m + 2) % 12 + 1;

  *year = y;
  *month = m;
  *day = d;
}

#endif /* MZXTEST_COMMON_HPP */
