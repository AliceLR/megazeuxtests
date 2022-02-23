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

  static bool create_directory(const char *filename, const char *destdir = nullptr);

  /* Implemented by Win32/POSIX specialized headers. */
  static FILE *io_tempfile(char (&dest)[TEMPFILE_SIZE]);
  static FILE *io_fopen(const char *path, const char *mode);
  static bool io_mkdir(const char *path, int mode);
  static bool io_unlink(const char *path);
  static bool io_rename(const char *old_path, const char *new_path);
  static int  io_get_file_type(const char *path);
  static bool set_file_times(const FileInfo &info, FILE *fp);
  static bool match_path(const char *path, const char *pattern);
  static bool clean_path_token(char *filename);
};

#endif /* MZXTEST_DIMGUTIL_FILEIO_HPP */
