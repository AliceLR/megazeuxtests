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

#ifndef MZXTEST_DIMGUTIL_DISKIMAGE_HPP
#define MZXTEST_DIMGUTIL_DISKIMAGE_HPP

#include <stdint.h>
#include <stdio.h>
#include <memory>

struct DiskList
{
  const char *start_base;
  const char *pattern;
  uint32_t directory;
  bool recursive;

  /* "Driver" implemented functions. */
  virtual bool Print(const char *base = nullptr, uint32_t index = UINT32_MAX) const = 0;
};

class DiskImage
{
public:
  const char *type;
  const char *media;
  bool error_state = false;

  DiskImage(const char *_type, const char *_media = nullptr): type(_type), media(_media) {}
  virtual ~DiskImage() {}

  /* "Driver" implemented functions. */
  virtual bool      PrintSummary() const = 0;
  virtual DiskList *CreateList(const char *filename, const char *pattern = nullptr, bool recursive = false) const = 0;

  /* Shorthand functions. */
  bool List(const char *filename, const char *pattern = nullptr, bool recursive = false) const
  {
    std::unique_ptr<DiskList> list(CreateList(filename, pattern, recursive));
    if(list && list->Print())
      return true;

    return false;
  }
};

class DiskImageLoader
{
public:
  DiskImageLoader();

  virtual DiskImage *Load(FILE *fp) const = 0;

  static DiskImage *TryLoad(FILE *fp);
};

#endif /* MZXTEST_DIMGUTIL_DISKIMAGE_HPP */
