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

#ifndef MZXTEST_DIMGUTIL_DISKIMAGE_HPP
#define MZXTEST_DIMGUTIL_DISKIMAGE_HPP

#include <stdint.h>
#include <stdio.h>
#include <memory>
#include <vector>

#include "FileInfo.hpp"

typedef std::vector<FileInfo> FileList;

class DiskImage
{
public:
  const char *type;
  const char *media;
  bool error_state = false;

  DiskImage(const char *_type, const char *_media = nullptr): type(_type), media(_media) {}
  virtual ~DiskImage() {}

  /* "Driver" implemented functions. */
  virtual bool PrintSummary() const = 0;
  virtual bool Search(FileList &dest, const FileInfo &filter, uint32_t filter_flags,
   const char *base, bool recursive = false) const = 0;
  virtual bool Test(const FileInfo &file) = 0;
  virtual bool Extract(const FileInfo &file, const char *destdir = nullptr) = 0;

  /* Shorthand functions. */
  bool Search(FileList &dest, const char *base, bool recursive = false) const
  {
    FileInfo dummy;
    uint32_t filter_flags = 0;
    return Search(dest, dummy, filter_flags, base, recursive);
  }
};

class DiskImageLoader
{
public:
  DiskImageLoader();
  virtual ~DiskImageLoader() {}

  virtual DiskImage *Load(FILE *fp, long file_length) const = 0;

  static DiskImage *TryLoad(FILE *fp, long file_length);
};

#endif /* MZXTEST_DIMGUTIL_DISKIMAGE_HPP */
