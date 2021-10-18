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

#ifndef MZXTEST_DIMGUTIL_FILEIO_HPP
#define MZXTEST_DIMGUTIL_FILEIO_HPP

#include <stdint.h>
#include <stdio.h>

#include "FileInfo.hpp"

class FileIO
{
public:
  static constexpr int TEMPFILE_SIZE = 260;

private:
  char path[TEMPFILE_SIZE];
  enum
  {
    INIT,
    OPEN,
    SUCCESS,
    ERROR,
  } state;

  FILE *fp;

public:
  FileIO(): state(INIT), fp(nullptr) {}
  ~FileIO();

  FILE *get_file();
  bool commit(const FileInfo &info, const char *destdir = nullptr);

  enum type
  {
    TYPE_UNKNOWN,
    TYPE_FILE,
    TYPE_DIR,
  };

  /* Implemented by Win32/POSIX specialized headers. */
  static FILE *io_tempfile(char (&dest)[TEMPFILE_SIZE]);
  static FILE *io_fopen(const char *path, const char *mode);
  static bool io_mkdir(const char *path, int mode);
  static bool io_unlink(const char *path);
  static bool io_rename(const char *old_path, const char *new_path);
  static int  io_get_file_type(const char *path);
  static bool set_file_times(const FileInfo &info, FILE *fp);
  static bool match_path(const char *path, const char *pattern);
};

#endif /* MZXTEST_DIMGUTIL_FILEIO_HPP */
