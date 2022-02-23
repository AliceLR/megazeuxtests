/**
 * Copyright (C) 2021 Lachesis <petrifiedrowan@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MZXTEST_DIMGUTIL_FILEINFO_HPP
#define MZXTEST_DIMGUTIL_FILEINFO_HPP

#include <stdint.h>
#include <time.h>

class FileInfo
{
public:
  enum flags
  {
    IS_DIRECTORY   = (1<<0),
    IS_VOLUME      = (1<<1),
    IS_DEVICE      = (1<<2),
    IS_INFO        = (1<<3),
    IS_LFN         = (1<<4),
    IS_REG         = (1<<5),

    TYPEMASK       = (IS_DIRECTORY | IS_VOLUME | IS_DEVICE | IS_INFO | IS_LFN | IS_REG),

    HAS_NAME_ALLOC = (1<<14),
    HAS_NAME_PTR   = (1<<15),
  };

  enum filter_flags
  {
    FILTER_NAME    = (1<<1),
    FILTER_SIZE_EQ = (1<<2),
    FILTER_SIZE_LT = (1<<3),
    FILTER_SIZE_GT = (1<<4),

    FILTER_FNMATCH = (1<<29), /* Use fnmatch instead of str[case]cmp for path compare. */
    FILTER_CASE_INSENSITIVE = (1<<30), /* Use case-insensitive path compare. */

    FILTER_SIZE    = (FILTER_SIZE_EQ | FILTER_SIZE_LT | FILTER_SIZE_GT),
  };

  enum checksum_type
  {
    NO_CHECKSUM,
    CRC16,   /* ARC/ArcFS, LHA */
    CRC32,   /* ZIP, gzip, LZX, etc. */
    CRC32_B, /* bzip2 */
  };

  static constexpr size_t NO_PACKING = SIZE_MAX;

  size_t size;
  size_t packed;

  /* Backreference to implementation-defined data for operations. */
  void *priv;

  /* Timestamps. Since conversion to/from unix time correctly for
   * different formats is a mess, the time format in hex is simply
   * a generic packed format which is easier to convert when-needed:
   *
   *   0xYYYYYYMMDDHHMMSS
   *
   * POSIX additionally has a status ("change") time
   * but it intentionally can't be set directly.
   */
  uint64_t access_d;
  uint64_t create_d;
  uint64_t modify_d;
  uint32_t access_ns;
  uint32_t create_ns;
  uint32_t modify_ns;

protected:
  uint16_t flags;
  uint16_t method;
  uint32_t crc;
  checksum_type crc_type = NO_CHECKSUM;

  /* Full path name relative to archive/filesystem root. */
  union
  {
    char buf[24];
    char *ptr;
  } path;

  void set_path_alloc(const char *base, const char *name);

public:
  FileInfo(): size(0), priv(0), flags(0) { path.ptr = nullptr; filetime(0); }

  FileInfo(const char *base, const char *name, int type, size_t file_size = 0, size_t file_packed = NO_PACKING, uint16_t method = 0);
  FileInfo(const FileInfo &src);
  FileInfo(FileInfo &&src);
  ~FileInfo();

  FileInfo &operator=(const FileInfo &src);
  FileInfo &operator=(FileInfo &&src);

  /* Set name string from temporary/stack storage. Copies/moves will allocate a duplicate string. */
  void set_path_external(char *buffer, size_t buffer_size, const char *base, const char *name);

  const char *name() const { return (flags & HAS_NAME_PTR) ? path.ptr : path.buf; }

  void set_type(int type) { flags = (flags & ~TYPEMASK) | (type & TYPEMASK); }
  int  get_type() const { return (flags & TYPEMASK); }

  void crc16(uint16_t _crc) { crc = _crc; crc_type = CRC16; }
  void crc32(uint32_t _crc) { crc = _crc; crc_type = CRC32; }

  /* Set all timestamps to one value (usually modified). */
  void filetime(uint64_t _date, uint32_t nsec = 0)
  {
    access(_date, nsec);
    create(_date, nsec);
    modify(_date, nsec);
  }

  void access(uint64_t _date, uint32_t nsec = 0)
  {
    access_d  = _date;
    access_ns = nsec;
  }

  void create(uint64_t _date, uint32_t nsec = 0)
  {
    create_d  = _date;
    create_ns = nsec;
  }

  void modify(uint64_t _date, uint32_t nsec = 0)
  {
    modify_d  = _date;
    modify_ns = nsec;
  }

  /* Compare this FileInfo to a filter template FileInfo.
   * Returns true on a match, otherwise false. */
  bool filter(const FileInfo &compare, uint32_t flg);

  /* Print summary. */
  void print() const;
  static void print_header();

  /* Convert DOS time to seconds. */
  static uint64_t convert_DOS(uint16_t _date, uint16_t _time)
  {
    int year   = ((_date & 0xfe00) >> 9) + 1980;
    int month  = ((_date & 0x01e0) >> 5) + 1;
    int day    = (_date & 0x001f) + 1;
    int hour   = ((_time & 0xf800) >> 11);
    int minute = ((_time & 0x07e0) >> 5);
    int second = (_time & 0x001f) * 2;

    return
     ((uint64_t)year << 40) | ((uint64_t)month << 32) |
     ((uint64_t)day << 24) | (hour << 16) | (minute << 8) | second;
  }

  /* Convert a tm to the FileInfo timestamp format. */
  static uint64_t convert_tm(const struct tm *tm)
  {
    return
     ((uint64_t)(tm->tm_year + 1900) << 40) | (((uint64_t)tm->tm_mon + 1) << 32) |
     ((uint64_t)tm->tm_mday << 24) | (tm->tm_hour << 16) | (tm->tm_min << 8) | tm->tm_sec;
  }

  static uint16_t date_year(uint64_t d)
  {
    return d >> 40;
  }

  static uint8_t date_month(uint64_t d)
  {
    return (d >> 32) & 0xff;
  }

  static uint8_t date_day(uint64_t d)
  {
    return (d >> 24) & 0xff;
  }

  static uint8_t time_hours(uint64_t d)
  {
    return (d >> 16) & 0xff;
  }

  static uint8_t time_minutes(uint64_t d)
  {
    return (d >> 8) & 0xff;
  }

  static uint8_t time_seconds(uint64_t d)
  {
    return d & 0xff;
  }
};

#endif /* MZXTEST_DIMGUTIL_FILEINFO_HPP */
