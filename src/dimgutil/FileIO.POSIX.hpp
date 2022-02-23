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

#ifndef MZXTEST_DIMGUTIL_FILEIO_POSIX_HPP
#define MZXTEST_DIMGUTIL_FILEIO_POSIX_HPP

#include "FileIO.hpp"

#include <fnmatch.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
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
  struct tm tm{};
  tm.tm_sec  = FileInfo::time_seconds(file_d);
  tm.tm_min  = FileInfo::time_minutes(file_d);
  tm.tm_hour = FileInfo::time_hours(file_d);
  tm.tm_mday = FileInfo::date_day(file_d);
  tm.tm_mon  = FileInfo::date_month(file_d) - 1;
  tm.tm_year = FileInfo::date_year(file_d) - 1900;

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

bool FileIO::clean_path_token(char *filename)
{
  // The only reserved chars generally are \0 (not an issue) and /.
  // Reject any values <32 since they're of questionable use and
  // potentially annoying or dangerous.
  while(*filename)
  {
    if(*filename == '/' || *filename < 32)
      *filename = '_';

    filename++;
  }
  return true;
}

#endif /* MZXTEST_DIMGUTIL_FILEIO_POSIX_HPP */
