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

//#include <fnmatch.h>
#include <string.h>
#include <utility>

#include "FileInfo.hpp"

#include "../common.hpp"

FileInfo::FileInfo(const char *base, const char *name, int _type, size_t _size):
 size(_size), flags(0)
{
  set_type(_type);
  set_path_alloc(base, name);
}

FileInfo::FileInfo(const FileInfo &src): flags(0)
{
  *this = src;
}

FileInfo::FileInfo(FileInfo &&src): flags(0)
{
  *this = std::move(src);
}

FileInfo::~FileInfo()
{
  if(flags & HAS_NAME_ALLOC)
    delete[] path.ptr;
}

FileInfo &FileInfo::operator=(const FileInfo &src)
{
  if(flags & HAS_NAME_ALLOC)
    delete[] path.ptr;

  size  = src.size;
  priv  = src.priv;
  flags = src.flags;
  access(src.access_d, src.access_ns);
  create(src.create_d, src.create_ns);
  modify(src.modify_d, src.modify_ns);

  if(flags & HAS_NAME_PTR)
  {
    // Duplicate even if the original isn't on the heap.
    size_t len = strlen(src.path.ptr);
    path.ptr = new char[len + 1];
    strcpy(path.ptr, src.path.ptr);
  }
  else
    memcpy(&path, &src.path, sizeof(path));

  return *this;
}

FileInfo &FileInfo::operator=(FileInfo &&src)
{
  if(flags & HAS_NAME_ALLOC)
    delete[] path.ptr;

  size  = src.size;
  priv  = src.priv;
  flags = src.flags;
  access(src.access_d, src.access_ns);
  create(src.create_d, src.create_ns);
  modify(src.modify_d, src.modify_ns);

  if((flags & HAS_NAME_PTR) && (flags & ~HAS_NAME_ALLOC))
  {
    // Duplicate *only* if the original isn't on the heap.
    // In this situation, it should be assumed to be stack allocated and temporary.
    size_t len = strlen(src.path.ptr);
    path.ptr = new char[len + 1];
    strcpy(path.ptr, src.path.ptr);
  }
  else

  if(flags & HAS_NAME_ALLOC)
  {
    path.ptr = src.path.ptr;
  }
  else
    memcpy(&path, &src.path, sizeof(path));

  src.flags &= ~(HAS_NAME_PTR | HAS_NAME_ALLOC);
  src.path.ptr = nullptr;
  return *this;
}

void FileInfo::set_path_alloc(const char *base, const char *name)
{
  char tmp[1024];
  if(base && base[0])
    snprintf(tmp, sizeof(tmp), "%s%c%s", base, DIR_SEPARATOR, name);
  else
    snprintf(tmp, sizeof(tmp), "%s", name);

  size_t len = path_clean_slashes(tmp);

  if(len >= sizeof(path.buf))
  {
    flags |= HAS_NAME_ALLOC | HAS_NAME_PTR;
    path.ptr = new char[len + 1];
    strcpy(path.ptr, tmp);
  }
  else
    strcpy(path.buf, tmp);
}

void FileInfo::set_path_external(char *buffer, size_t buffer_size, const char *base, const char *name)
{
  if(flags & HAS_NAME_ALLOC)
  {
    flags &= ~HAS_NAME_ALLOC;
    delete[] path.ptr;
  }
  if(base && base[0])
    snprintf(buffer, buffer_size, "%s%c%s", base, DIR_SEPARATOR, name);
  else
    snprintf(buffer, buffer_size, "%s", name);

  path_clean_slashes(buffer);
  path.ptr = buffer;
  flags |= HAS_NAME_PTR;
}

bool FileInfo::filter(const FileInfo &compare, uint32_t flg)
{
  if(!flg)
    return true;

  // FIXME fnmatch!!!
  if(flg & FILTER_NAME)
  {
    if(flg & FILTER_CASE_INSENSITIVE)
    {
      if(!strcasecmp(name(), compare.name()))
        return true;
    }
    else
    {
      if(!strcmp(name(), compare.name()))
        return true;
    }
  }

  if(flg & FILTER_SIZE)
  {
    if((flg & FILTER_SIZE_EQ) && size == compare.size)
      return true;
    if((flg & FILTER_SIZE_LT) && size < compare.size)
      return true;
    if((flg & FILTER_SIZE_GT) && size > compare.size)
      return true;
  }

  return false;
}

void FileInfo::print() const
{
  char size_str[16];
  if(flags & IS_LFN)
  {
    strcpy(size_str, "<LFN>");
  }
  else

  if(flags & IS_VOLUME)
  {
    strcpy(size_str, "<VOLUME>");
  }
  else

  if(flags & IS_DEVICE)
  {
    strcpy(size_str, "<DEVICE>");
  }
  else

  if(flags & IS_DIRECTORY)
  {
    strcpy(size_str, "<DIR>");
  }
  else

  if(flags & IS_INFO)
  {
    strcpy(size_str, "<INFO>");
  }
  else
    snprintf(size_str, sizeof(size_str), "%15zu", size);

  fprintf(stderr, "%6u-%02u-%02u %02u:%02u:%02u  :  %-15.15s  : %s\n",
    (unsigned)(modify_d >> 40), (unsigned)((modify_d >> 32) & 0xff), (unsigned)((modify_d >> 24) & 0xff),
    (unsigned)((modify_d >> 16) & 0xff), (unsigned)((modify_d >> 8) & 0xff), (unsigned)(modify_d & 0xff),
    size_str, name()
  );
}
