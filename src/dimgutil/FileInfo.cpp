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

//#include <fnmatch.h>
#include <string.h>
#include <utility>

#include "FileInfo.hpp"

#include "../common.hpp"

static constexpr int CHECKSUM_WIDTHS[] =
{
  0,
  4,
  8,
  8,
};

FileInfo::FileInfo(const char *base, const char *name, int _type, size_t _size, size_t _packed, uint16_t _method):
 size(_size), packed(_packed != NO_PACKING ? _packed : _size), flags(0), method(_method)
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

  size     = src.size;
  packed   = src.packed;
  priv     = src.priv;
  flags    = src.flags;
  method   = src.method;
  crc      = src.crc;
  crc_type = src.crc_type;
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

  size     = src.size;
  packed   = src.packed;
  priv     = src.priv;
  flags    = src.flags;
  method   = src.method;
  crc      = src.crc;
  crc_type = src.crc_type;
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

  char crc_str[16]{};
  if(crc_type != NO_CHECKSUM)
    snprintf(crc_str, sizeof(crc_str), "%0*x", CHECKSUM_WIDTHS[crc_type], crc);

  fprintf(stderr, "%6u-%02u-%02u %02u:%02u:%02u  :  %-15.15s  :  %10zu  : %8s : %4Xh  : %s\n",
    date_year(modify_d), date_month(modify_d), date_day(modify_d),
    time_hours(modify_d), time_minutes(modify_d), time_seconds(modify_d),
    size_str, packed, crc_str, method, name()
  );
}

void FileInfo::print_header()
{
  static constexpr const char LINES[] = "--------------------";
  fprintf(stderr, "  %-19.19s     %-15.15s    %-11.11s    %-8.8s   %-6.6s   %-8.8s\n",
   "Modified", "Type/size", "Stored size", "CRC", "Method", "Filename");
  fprintf(stderr, "  %-19.19s  :  %-15.15s  : %-11.11s  : %-8.8s : %-6.6s : %-8.8s\n",
   LINES, LINES, LINES, LINES, LINES, LINES);
}
