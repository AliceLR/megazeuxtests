/**
 * dimgutil: disk image and archive utility
 * Copyright (C) 2022 Alice Rowan <petrifiedrowan@gmail.com>
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

/**
 * Unpacker for Amiga LZX archives.
 * This format is the direct predecessor to Microsoft CAB files.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "DiskImage.hpp"
#include "FileIO.hpp"
#include "crc32.h"
#include "lzx_unpack.h"

#include "../format.hpp"

enum LZX_method
{
  LZX_UNPACKED = LZX_M_UNPACKED,
  LZX_PACKED   = LZX_M_PACKED,
  LZX_EOF      = 32,
  LZX_INVALID  = -1
};

class LZX_MachineType
{
public:
  const enum type
  {
    MS_DOS  = 0,
    WINDOWS = 1,
    OS_2    = 2,
    AMIGA   = 10,
    UNIX    = 20,
  } value;

  LZX_MachineType(int i): value(static_cast<type>(i)) {}

  constexpr const char *string() const
  {
    switch(value)
    {
      case MS_DOS:  return "MS-DOS";
      case WINDOWS: return "Windows";
      case OS_2:    return "OS/2";
      case AMIGA:   return "Amiga";
      case UNIX:    return "Unix";
    }
    return "unknown";
  }
};

struct LZX_header
{
  /*  0    char     magic[3];   */  // LZX
  /*  3    uint8_t  flags;      */  // Claimed by unlzx.c
  /*  4    uint8_t  unknown[6]; */
  /* 10 */

  static constexpr size_t HEADER_SIZE = 10;
  uint8_t data[HEADER_SIZE];

  bool is_valid() const
  {
    if(memcmp(data, "LZX", 3))
      return false;

    return true;
  }
};

struct LZX_entry
{
  /*  0    uint16_t type; */              // ?
  /*  2    uint32_t uncompressed_size; */
  /*  6    uint32_t compressed_size; */
  /* 10    uint8_t  machine_type; */      // unlzx.c
  /* 11    uint8_t  method; */            // unlzx.c
  /* 12    uint8_t  flags; */             // unlzx.c
  /* 13    uint8_t  unknown1; */
  /* 14    uint8_t  comment_length; */    // unlzx.c
  /* 15    uint8_t  extract_version; */   // unlzx.c
  /* 16    uint16_t unknown2; */
  /* 18    uint32_t datestamp; */         // unlzx.c
  /* 22    uint32_t crc; */
  /* 26    uint32_t header_crc; */        // unlzx.c
  /* 30    uint8_t  filename_length; */
  /* 31 */

  static constexpr int ENTRY_SIZE = 31;
  uint8_t data[ENTRY_SIZE];

  enum flags
  {
    MERGED = (1 << 0),
  };

  uint32_t uncompressed_size() const
  {
    return mem_u32le(data + 2);
  }

  uint32_t compressed_size() const
  {
    return mem_u32le(data + 6);
  }

  LZX_MachineType machine() const
  {
    return LZX_MachineType(data[10]);
  }

  unsigned int method() const
  {
    return data[11];
  }

  LZX_method _method() const
  {
    switch(data[11])
    {
      case LZX_UNPACKED:
      case LZX_PACKED:
      case LZX_EOF:
        return static_cast<LZX_method>(data[11]);
    }
    return LZX_INVALID;
  }

  unsigned int flags() const
  {
    return data[12];
  }

  bool is_merged() const
  {
    return !!(data[12] & MERGED);
  }

  unsigned int comment_length() const
  {
    return data[14];
  }

  unsigned int extract_version() const
  {
    return data[15];
  }

  uint32_t datestamp() const
  {
    return mem_u32be(data + 18);
  }

  uint32_t crc() const
  {
    return mem_u32le(data + 22);
  }

  uint32_t header_crc() const
  {
    return mem_u32le(data + 26);
  }

  unsigned int filename_length() const
  {
    return data[30];
  }

  void filename(char (&buffer)[256]) const
  {
    size_t len = filename_length();
    memcpy(buffer, data + 31, len);
    buffer[len] = '\0';
  }

  void comment(char (&buffer)[256]) const
  {
    size_t len = comment_length();
    memcpy(buffer, data + 31 + filename_length(), len);
    buffer[len] = '\0';
  }

  // Get the header length.
  size_t header_length() const
  {
    return ENTRY_SIZE + filename_length() + comment_length();
  }

  // Get the offset to the contents of this file.
  const uint8_t *data_pointer() const
  {
    const uint8_t *here = reinterpret_cast<const uint8_t *>(this);
    return here + header_length();
  }

  /**
   * Can this file be decompressed?
   * Returns true if the extract version and method are supported,
   * and if the header CRC matches the stored header CRC value.
   * This function assumes the entry has already been bounds checked.
   */
  bool can_decompress() const
  {
    if(extract_version() > 0x0a)
      return false;

    if(_method() == LZX_INVALID)
      return false;

    /* Merged + uncompressed is nonsense... */
    if(method() == LZX_UNPACKED && is_merged())
      return false;

    uint8_t tmp[ENTRY_SIZE + 255 + 255];
    size_t size = header_length();
    memcpy(tmp, data, size);
    memset(tmp + 26, '\0', 4); // header_crc wasn't known when the CRC was taken!

    return header_crc() == dimgutil_crc32(0, tmp, size);
  }

  /**
   * Make sure the header, extended header data, and compressed data
   * all fit within the current file.
   */
  bool is_valid(const uint8_t *data_end) const
  {
    const uint8_t *here = reinterpret_cast<const uint8_t *>(this);
    if(here >= data_end || ENTRY_SIZE > data_end - here)
      return false;

    ptrdiff_t size = header_length();
    if(size > data_end - here)
      return false;
    if(compressed_size() > data_end - here - size)
      return false;

    return true;
  }

  /**
   * Get the next header for an LZX_entry that exists in a buffer in RAM.
   * The returned LZX_entry * will be a pointer to the next LZX_entry in
   * that buffer. This assumes the current entry is valid.
   */
  LZX_entry *next_entry(const uint8_t *data_end)
  {
    LZX_entry *next = reinterpret_cast<LZX_entry *>(data + header_length() + compressed_size());
    if(!next->is_valid(data_end))
      return nullptr;

    return next;
  }

  /**
   * Get the first header for an LZX_entry that exists in a buffer in RAM.
   */
  static LZX_entry *first_entry(uint8_t *data_start, const uint8_t *data_end)
  {
    LZX_entry *first = reinterpret_cast<LZX_entry *>(data_start + LZX_header::HEADER_SIZE);
    if(!first->is_valid(data_end))
      return nullptr;

    return first;
  }

  /**
   * Depack stored date.
   *
   * Quoted from unlzx.c:
   *
   *  "UBYTE packed[4]; bit 0 is MSB, 31 is LSB
   *   bit # 0-4=Day 5-8=Month 9-14=Year 15-19=Hour 20-25=Minute 26-31=Second"
   *
   * Normal packing for these is: year 0=1970, month 0=January, day 1=1.
   * The original program uses the following formula to derive the packed
   * year, which is flawed for obvious reasons:
   *
   *  year = ([2 digit year] - 70) & 63
   *
   * This means it outputs 111010b for 2000, 111011b for 2001, etc, until it
   * rolls back to 1970 (instead of 2006).
   *
   * The Mikolaj Calusinski fix addresses the algorithm itself so values
   * 30-63 correspond to years 2000 to 2033. This fix is or was apparently
   * used by xadmaster.
   *
   * The Dr. Titus fix is poorly documented but does confirm some key things,
   * such as the "rollback [...] after the year of 2006". It mentions a
   * "six-month count system" and it's not clear what that actually means,
   * since no bits of precision were repurposed to the year, considering the
   * expanded years:
   *
   *   "Expanded  years  range  to  2041  by  using reserved (in LZX only)
   *    year numbers 1970-1977, which aren't used by AmigaDos"
   *
   * This fix appears to use 111010b through 111111b for years 2000-2005,
   * then uses 011110b through 111001b for 2006-2033, and finally 000000b
   * through 000111b for 2034-2041. Classic Workbench uses this version of LZX.
   */
  uint64_t get_fileinfo_date() const
  {
    uint32_t ts = datestamp();
    int seconds = (ts &       0x3fUL);
    int minutes = (ts &      0xfc0UL) >> 6;
    int hours   = (ts &    0x1f000UL) >> 12;
    int day     = (ts & 0xf8000000UL) >> 27;
    int month   = (ts & 0x07800000UL) >> 23;
    int year    = (ts & 0x007e0000UL) >> 17;

    /* Dr. Titus datestamps: */
    /* 2000 to 2005 (compatible with the original buggy LZX) */
    if(year >= 0b111010 && year <= 0b111111)
      year += (2000 - 2028);
    else

    /* 2006 to 2033 */
    if(year >= 0b011110 && year <= 0b111001)
      year += (2006 - 2000);
    else

    /* 2034 to 2041 */
    if(year < (1978 - 1970))
      year += (2034 - 1970);

    struct tm tm{};
    tm.tm_sec  = seconds;
    tm.tm_min  = minutes;
    tm.tm_hour = hours;
    tm.tm_mday = day;
    tm.tm_mon  = month;
    tm.tm_year = year + (1970 - 1900);

    return FileInfo::convert_tm(&tm);
  }
};

struct LZXMergeEntry
{
  LZX_entry *entry;
  size_t offset;
};

class LZXMerge
{
public:
  LZX_entry *first = nullptr;
  LZX_entry *last = nullptr;
  uint8_t *buffer = nullptr;
  size_t total_uncompressed = 0;

  std::vector<LZXMergeEntry> positions;

  ~LZXMerge()
  {
    free(buffer);
  }

  void add(LZX_entry *e)
  {
    if(!first)
      first = e;
    last = e;

    positions.push_back({ e, total_uncompressed });
    total_uncompressed += e->uncompressed_size();
  }

  bool has_entry(const LZX_entry *e) const
  {
    return first && last && e >= first && e <= last;
  }

  size_t init_buffer()
  {
    if(!buffer)
    {
      buffer = (uint8_t *)malloc(total_uncompressed);
      if(!buffer)
        return 0;
    }
    return total_uncompressed;
  }
};

class LZXImage: public DiskImage
{
  LZX_header header;
  LZX_entry *entry_start = nullptr;
  std::vector<LZXMerge> merged;
  uint8_t *data = nullptr;
  uint8_t *data_end;
  size_t data_length;

public:
  LZXImage(LZX_header &_h, FILE *fp, long file_length):
   DiskImage::DiskImage("LZX", "Archive"), header(_h), data_length(file_length)
  {
    data = new uint8_t[file_length];
    if(!fread(data, file_length, 1, fp))
    {
      error_state = true;
      return;
    }
    data_end = data + data_length;
    entry_start = LZX_entry::first_entry(data, data_end);

    /* Construct merge table for decompression later. */
    LZXMerge *current = nullptr;
    for(LZX_entry *h = entry_start; h; h = h->next_entry(data_end))
    {
      if(h->is_merged())
      {
        if(!current)
        {
          merged.push_back({});
          current = &merged.back();
        }
        current->add(h);
        /* A merge ends when a compressed size is encountered. */
        if(h->compressed_size())
          current = nullptr;
      }
      else
        current = nullptr;
    }
  }
  virtual ~LZXImage()
  {
    delete[] data;
  }

  /* "Driver" implemented functions. */
  virtual bool PrintSummary() const override;
  virtual bool Search(FileList &list, const FileInfo &filter, uint32_t filter_flags,
   const char *base, bool recursive = false) const override;
  virtual bool Extract(const FileInfo &file, const char *destdir = nullptr) override;

  LZX_entry *get_entry(const char *path) const;
};

bool LZXImage::PrintSummary() const
{
  if(error_state)
    return false;

  format::line("Type",     "%s", type);
  format::line("Media",    "%s", media);
  format::line("Size",     "%zu", data_length);
  return true;
}

bool LZXImage::Search(FileList &list, const FileInfo &filter, uint32_t filter_flags,
 const char *base, bool recursive) const
{
  if(error_state)
    return false;

  LZX_entry *h;
  char prefix[256];
  size_t prefix_len = 0;
  if(base && base[0])
  {
    h = get_entry(base);
    if(h)
    {
      // LZX doesn't store subdirectories, so base is a file.
      FileInfo tmp("", base, FileInfo::IS_REG, h->uncompressed_size(), h->compressed_size(), (h->flags() << 8) | h->method());
      tmp.priv = h;
      tmp.crc32(h->crc());
      tmp.filetime(h->get_fileinfo_date(), 0);
      if(tmp.filter(filter, filter_flags))
        list.push_back(std::move(tmp));

      return true;
    }

    // Base is a directory prefix or nonsense.
    // Assume directory prefix and add a trailing slash to disambiguate this.
    snprintf(prefix, sizeof(prefix), "%s%c", base, DIR_SEPARATOR);
    prefix_len = path_clean_slashes(prefix);
  }

  char filename[256];
  for(h = entry_start; h; h = h->next_entry(data_end))
  {
    h->filename(filename);
    path_clean_slashes(filename);

    if(prefix_len && strncasecmp(base, filename, prefix_len))
      continue;

    FileInfo tmp("", filename, FileInfo::IS_REG, h->uncompressed_size(), h->compressed_size(), (h->flags() << 8) | h->method());
    tmp.priv = h;
    tmp.crc32(h->crc());
    tmp.filetime(h->get_fileinfo_date(), 0);

    if(tmp.filter(filter, filter_flags))
      list.push_back(std::move(tmp));
  }
  return true;
}

bool LZXImage::Extract(const FileInfo &file, const char *destdir)
{
  LZX_entry *h = reinterpret_cast<LZX_entry *>(file.priv);

  /* Check file for extractability... */
  if(!h->can_decompress())
  {
    format::warning("decompressing file is unsupported");
    return true;
  }

  /* In LZX all entries are files, so make sure the parent exists. */
  char pathname[1024];
  const char *pos = strrchr(file.name(), DIR_SEPARATOR);
  if(pos)
  {
    size_t len = pos - file.name();
    memcpy(pathname, file.name(), len);
    pathname[len] = '\0';

    if(!FileIO::create_directory(pathname, destdir))
    {
      format::error("failed mkdir");
      return false;
    }
  }

  /* Extract file. */
  const uint8_t *input = h->data_pointer();
  size_t input_size = h->compressed_size();
  unsigned int method = h->method();

  uint8_t *output;
  size_t output_size;

  FileIO output_file;
  FILE *fp = output_file.get_file();
  if(!fp)
    return false;

  std::unique_ptr<uint8_t> _free_me(nullptr);

  if(h->is_merged())
  {
    /* Find relevant merge record. */
    LZXMerge *merge = nullptr;
    size_t merge_offset;
    for(LZXMerge &m : merged)
    {
      if(m.has_entry(h))
      {
        /* Get exact entry in merge. */
        for(const LZXMergeEntry &me : m.positions)
        {
          if(me.entry == h)
          {
            merge = &m;
            merge_offset = me.offset;
            break;
          }
        }
        if(merge)
          break;
      }
    }
    if(!merge || merge->last->compressed_size() == 0)
    {
      format::warning("skipping broken merged file");
      return true;
    }

    /* Depack the merged record. */
    if(!merge->buffer)
    {
      if(!merge->init_buffer())
      {
        format::warning("failed to allocate buffer for merge file");
        return true;
      }

      LZX_entry *last = merge->last;
      input = last->data_pointer();
      input_size = last->compressed_size();
      method = last->method();

      const char *err = lzx_unpack(merge->buffer, merge->total_uncompressed, input, input_size, method);
      if(err)
      {
        format::error("%s (%Xh)", err, static_cast<unsigned>(method));
        return true;
      }
    }

    output = merge->buffer + merge_offset;
    output_size = h->uncompressed_size();
  }
  else

  if(method != LZX_UNPACKED)
  {
    output_size = h->uncompressed_size();
    output = new uint8_t[output_size];
    _free_me.reset(output);

    const char *err = lzx_unpack(output, output_size, input, input_size, method);
    if(err)
    {
      format::error("%s (%Xh)", err, static_cast<unsigned>(method));
      return true;
    }
  }
  else
  {
    output = const_cast<uint8_t *>(input);
    output_size = input_size;
  }

  uint32_t crc = dimgutil_crc32(0, output, output_size);
  if(crc != h->crc())
    format::warning("CRC-32 mismatch: expected 0x%08x, got 0x%08x", h->crc(), crc);

  if(fwrite(output, 1, output_size, fp) != output_size)
    return false;

  return output_file.commit(file, destdir);
}

LZX_entry *LZXImage::get_entry(const char *path) const
{
  char buffer[1024];
  char filename[256];
  LZX_entry *h;

  snprintf(buffer, sizeof(buffer), "%s", path);
  path_clean_slashes(buffer);

  for(h = entry_start; h; h = h->next_entry(data_end))
  {
    h->filename(filename);
    path_clean_slashes(filename);

    if(!strcasecmp(buffer, filename))
      return h;
  }
  return nullptr;
}


class LZXLoader: public DiskImageLoader
{
public:
  virtual DiskImage *Load(FILE *fp, long file_length) const override
  {
    LZX_header h{};

    if(!fread(h.data, sizeof(h.data), 1, fp))
      return nullptr;

    if(!h.is_valid())
      return nullptr;

    rewind(fp);
    return new LZXImage(h, fp, file_length);
  }
};

static const LZXLoader loader;
