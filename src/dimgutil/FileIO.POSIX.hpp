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

#ifndef MZXTEST_DIMGUTIL_FILEIO_POSIX_HPP
#define MZXTEST_DIMGUTIL_FILEIO_POSIX_HPP

#include "FileIO.hpp"

#include <fnmatch.h>
#include <time.h>
#include <sys/stat.h>

FILE *FileIO::io_tempfile(char (&dest)[TEMPFILE_SIZE])
{
  // TODO other tempfile dirs for Android, etc.
  static constexpr const char TEMPLATE[] = "/tmp/dimgutil_XXXXXX";
  snprintf(dest, sizeof(dest), TEMPLATE);

  int fd = mkstemp(dest);
  if(fd >= 0)
  {
    FILE *fp = fdopen(fd, "w+b");
    if(!fp)
      close(fd);

    return fp;
  }
  return nullptr;
}

FILE *FileIO::io_fopen(const char *path, const char *mode)
{
  return fopen(path, mode);
}

bool FileIO::io_mkdir(const char *path, int mode)
{
  return mkdir(path, mode) == 0;
}

bool FileIO::io_unlink(const char *path)
{
  return unlink(path) == 0;
}

bool FileIO::io_rename(const char *old_path, const char *new_path)
{
  return rename(old_path, new_path) == 0;
}

int FileIO::io_get_file_type(const char *path)
{
  struct stat st;
  if(stat(path, &st) == 0)
  {
    if(S_ISREG(st.st_mode))
      return TYPE_FILE;
    if(S_ISDIR(st.st_mode))
      return TYPE_DIR;
  }
  return TYPE_UNKNOWN;
}

static inline bool convert_time(struct timespec &dest, uint64_t file_d, uint32_t file_ns)
{
  struct tm tm
  {
    /* tm_sec   */ time_seconds(file_d),
    /* tm_min   */ time_minutes(file_d),
    /* tm_hour  */ time_hours(file_d),
    /* tm_mday  */ date_day(file_d),
    /* tm_mon   */ date_month(file_d) - 1,
    /* tm_year  */ date_year(file_d) - 1900,
    0,
    0,
    /* tm_isdst */ 0,
  };
  dest.tv_sec = mktime(&tm);
  dest.tv_nsec = file_ns;
  return dest.tv_sec >= 0;
}

bool FileIO::set_file_times(const FileInfo &info, FILE *fp)
{
  int fd = fileno(fp);
  if(fd >= 0)
  {
    struct timespec times[2]{};
    if(!convert_time(times[0], info.access_d, info.access_ns))
      times[0].tv_nsec = UTIME_OMIT;
    if(!convert_time(times[1], info.modify_d, info.modify_ns))
      times[1].tv_nsec = UTIME_OMIT;

    return futimens(fd, times) == 0;
  }
  return false;
}

bool FileIO::match_path(const char *path, const char *pattern)
{
  return fnmatch(pattern, path, FNM_PATHNAME|FNM_NOESCAPE) == 0;
}

#endif /* MZXTEST_DIMGUTIL_FILEIO_POSIX_HPP */
