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

/**
 * Unpacker for ArcFS archives.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "DiskImage.hpp"
#include "FileIO.hpp"
#include "arc_unpack.h"
#include "crc32.h"

#include "../format.hpp"


enum ArcFS_type
{
  END_OF_DIR = 0,
  DELETED    = 1,
  UNPACKED   = 2, // stored 0x82
  PACKED     = 3, // stored 0x83
  SQUEEZED   = 4, // stored 0x84
  CRUNCHED   = 8, // stored 0x88
  SQUASHED   = 9, // stored 0x89

  SPARK_UNPACKED   = 0x82,
  SPARK_PACKED     = 0x83,
  SPARK_SQUEEZED   = 0x84,
  SPARK_CRUNCHED   = 0x88,
  SPARK_SQUASHED   = 0x89,
  SPARK_COMPRESSED = 0xff,

  ARCFS_INVALID = -1,
};

static constexpr int ARCFS_HEADER_SIZE = 96;

struct ArcFS_header
{
  //*  0 */ char     magic[8];       // Archive\000
  //*  8 */ uint32_t entries_length; // multiple of 36
  //* 12 */ uint32_t data_offset;
  //* 16 */ uint32_t min_read_version;
  //* 20 */ uint32_t min_write_version;
  //* 24 */ uint32_t format_version;
  //* 28 */ uint8_t  reserved[68];
  /* 96 */

  uint8_t data[ARCFS_HEADER_SIZE];

  uint32_t entries_length() const
  {
    return mem_u32le(data + 8);
  }

  uint32_t data_offset() const
  {
    return mem_u32le(data + 12);
  }

  uint32_t min_read_version() const
  {
    return mem_u32le(data + 16);
  }

  uint32_t min_write_version() const
  {
    return mem_u32le(data + 20);
  }

  uint32_t format_version() const
  {
    return mem_u32le(data + 24);
  }

  bool is_valid() const
  {
    if(memcmp(data, "Archive\x00", 8))
      return false;

    // Entries are always 36 bytes long, so the entries length should be a multiple of 36.
    // The stored data should not preceed the end of the entries area.
    if(entries_length() % 36 || data_offset() < ARCFS_HEADER_SIZE + entries_length())
      return false;

    // Highest version of ArcFS seems to be 2.60.
    if(min_read_version() > 260 || min_write_version() > 260)
      return false;

    // Highest format version seems to be 0x0a.
    if(format_version() > 0x0a)
      return false;

    return true;
  }
};

struct ArcFS_entry
{
  //*  0 */ uint8_t  type;
  //*  1 */ char     filename[11];
  //* 12 */ uint32_t uncompressed_size;
  //* 16 */ uint32_t load_offset;
  //* 20 */ uint32_t exec_offset; // ??????
  //* 24 */ uint32_t attributes;
  //* 28 */ uint32_t compressed_size;
  //* 32 */ uint32_t info;
  /* 36 */

  static constexpr int ARCFS_ENTRY_SIZE = 36;

  uint8_t data[ARCFS_ENTRY_SIZE];

  ArcFS_type type() const
  {
    switch(data[0])
    {
      case END_OF_DIR:
      case DELETED:
      case UNPACKED:
      case PACKED:
      case SQUEEZED:
      case CRUNCHED:
      case SQUASHED:
      case SPARK_UNPACKED:
      case SPARK_PACKED:
      case SPARK_SQUEEZED:
      case SPARK_CRUNCHED:
      case SPARK_SQUASHED:
        return static_cast<ArcFS_type>(data[0] & 0x7f); // & 0x7f to convert Spark types to normal.

      case SPARK_COMPRESSED:
        return SPARK_COMPRESSED; // Leave as 255.
    }
    return ARCFS_INVALID;
  }

  const char *filename() const
  {
    return reinterpret_cast<const char *>(data + 1);
  }

  uint32_t uncompressed_size() const
  {
    if(is_directory())
      return 0;
    return mem_u32le(data + 12);
  }

  uint32_t load_address() const
  {
    return mem_u32le(data + 16);
  }

  uint32_t exec_address() const
  {
    return mem_u32le(data + 20);
  }

  // attributes (@ 24) splits into four fields:
  // * crc16 (@ 26)
  // * compression bits (@ 25)
  // * permissions (@ 24)

  uint8_t permissions() const
  {
    return data[24];
  }

  uint8_t compression_bits() const
  {
    return data[25];
  }

  uint16_t crc16() const
  {
    return mem_u16le(data + 26);
  }

  uint32_t compressed_size() const
  {
    if(is_directory())
      return 0;
    return mem_u32le(data + 28);
  }

  uint32_t info_word() const
  {
    return mem_u32le(data + 32);
  }

  uint32_t data_offset() const
  {
    // Lower 31 bits of object information word determines the offset in file.
    // For a file, a value of 0 corresponds to the archive start of data offset.
    // For a directory, a value of 0 corresponds to the end of the archive header (position 96).
    // The value a directory points to is the NEXT entry in the current directory;
    // the following entry is the first entry of this directory.
    return info_word() & 0x7fffffffUL;
  }

  bool is_directory() const
  {
    // Highest bit of object information word determines file (0) or directory (1).
    return (data[35] & 0x80) != 0;
  }

  uint64_t get_timestamp() const
  {
    // The documentation is vague, but both directories and files seem to have datestamps.
    // Datestamps are measured in centiseconds starting from Jan 1st, 1900.

    // Low byte of load address + exec address.
    return ((uint64_t)data[16] << 32) | exec_address();
  }

  /**
   * Get the next header for an ArcFS_entry that exists in a buffer in RAM.
   * The returned ArcFS_entry * will be a pointer to the next ArcFS_entry in
   * that buffer.
   */
  ArcFS_entry *next_header(ArcFS_entry *entry_start, ArcFS_entry *entry_end)
  {
    if(this < entry_start || this >= entry_end)
      return nullptr;

    ArcFS_type t = type();
    if(t == ARCFS_INVALID || t == END_OF_DIR)
      return nullptr;

    ArcFS_entry *ret;
    if(is_directory())
    {
      uint32_t offset = data_offset() / ARCFS_ENTRY_SIZE;
      ret = entry_start + offset;
    }
    else
      ret = this + 1;

    if(ret >= entry_end)
      return nullptr;

    t = ret->type();
    if(t == ARCFS_INVALID || t == END_OF_DIR)
      return nullptr;

    ret->data[11] = '\0';
    return ret;
  }

  /**
   * Get the first header of a directory ArcFS_entry in RAM.
   */
  ArcFS_entry *subdirectory_header(ArcFS_entry *entry_start, ArcFS_entry *entry_end)
  {
    if(this < entry_start || this + 1 >= entry_end)
      return nullptr;

    ArcFS_type t = type();
    if(t == ARCFS_INVALID || t == END_OF_DIR || !is_directory())
      return nullptr;

    ArcFS_entry *ret = this + 1;
    t = ret->type();
    if(t == ARCFS_INVALID || t == END_OF_DIR)
      return nullptr;

    ret->data[11] = '\0';
    return ret;
  }

  int get_filetype() const
  {
    if(is_directory())
      return FileInfo::IS_DIRECTORY;
    return FileInfo::IS_REG;
  }

  uint64_t get_fileinfo_date() const
  {
    uint64_t ts = get_timestamp() / 100;
    if(!ts)
      return 0;

    static constexpr uint64_t EPOCH = date_to_total_days(1900, 1, 1);

    int seconds = ts % 60;
    int minutes = (ts / 60) % 60;
    int hours = (ts / 3600) % 24;
    int day;
    int month;
    int year;
    uint64_t total_days = ts / 86400 + EPOCH;
    total_days_to_date(total_days, &year, &month, &day);

    struct tm tm{};
    tm.tm_sec  = seconds;
    tm.tm_min  = minutes;
    tm.tm_hour = hours;
    tm.tm_mday = day;
    tm.tm_mon  = month - 1;
    tm.tm_year = year - 1900;

    return FileInfo::convert_tm(&tm);
  }

  uint32_t get_fileinfo_ns() const
  {
    return (get_timestamp() % 100) * 10 * 10000000;
  }
};

class ArcFSImage: public DiskImage
{
  ArcFS_header header;
  ArcFS_entry *entry_start;
  ArcFS_entry *entry_end;
  std::unique_ptr<uint8_t[]> held_buffer;
  uint8_t *data = nullptr;
  size_t data_length;

public:
  ArcFSImage(ArcFS_header &_h, FILE *fp, long file_length):
   DiskImage::DiskImage("ArcFS", "Archive"), header(_h), data_length(file_length)
  {
    data = new uint8_t[file_length];
    if(!fread(data, file_length, 1, fp))
    {
      error_state = true;
      return;
    }
    entry_start = reinterpret_cast<ArcFS_entry *>(data + ARCFS_HEADER_SIZE);
    entry_end = reinterpret_cast<ArcFS_entry *>(data + ARCFS_HEADER_SIZE + header.entries_length());
  }
  virtual ~ArcFSImage()
  {
    delete[] data;
  }

  /* "Driver" implemented functions. */
  virtual bool PrintSummary() const override;
  virtual bool Search(FileList &list, const FileInfo &filter, uint32_t filter_flags,
   const char *base, bool recursive = false) const override;
  virtual bool Test(const FileInfo &file) override;
  virtual bool Extract(const FileInfo &file, const char *destdir = nullptr) override;

  void search_r(FileList &list, const FileInfo &filter, uint32_t filter_flags,
   const char *base, bool recursive, ArcFS_entry *h) const;
  bool unpack_file(const FileInfo &file, uint8_t **dest, size_t *dest_len, uint16_t *dest_crc);
  ArcFS_entry *get_entry(const char *path) const;
};

bool ArcFSImage::PrintSummary() const
{
  if(error_state)
    return false;

  format::line("Type",     "%s v%" PRIu32, type, header.format_version());
  format::line("Media",    "%s", media);
  format::line("Size",     "%zu", data_length);
  format::line("ReadVer",  "%" PRIu32, header.min_read_version());
  format::line("WriteVer", "%" PRIu32, header.min_write_version());
  return true;
}

bool ArcFSImage::Search(FileList &list, const FileInfo &filter, uint32_t filter_flags,
 const char *base, bool recursive) const
{
  if(error_state)
    return false;

  ArcFS_entry *h;
  if(base && base[0])
  {
    h = get_entry(base);
    if(!h)
      return false;

    if(!h->is_directory())
    {
      // Base is file.
      FileInfo tmp("", base, h->get_filetype(), h->uncompressed_size(), h->compressed_size(), h->data[0]);
      tmp.priv = h;
      tmp.crc16(h->crc16());
      tmp.filetime(h->get_fileinfo_date(), h->get_fileinfo_ns());
      if(tmp.filter(filter, filter_flags))
        list.push_back(std::move(tmp));

      return true;
    }
  }
  else
    h = entry_start;

  search_r(list, filter, filter_flags, base, recursive, h);
  return true;
}

void ArcFSImage::search_r(FileList &list, const FileInfo &filter, uint32_t filter_flags,
 const char *base, bool recursive, ArcFS_entry *h) const
{
  std::vector<ArcFS_entry *> dirs;

  do
  {
    if(h->type() == DELETED)
      continue;

    if(recursive && h->is_directory())
      dirs.push_back(h);

    FileInfo tmp(base, h->filename(), h->get_filetype(), h->uncompressed_size(), h->compressed_size(), h->data[0]);
    tmp.priv = h;
    tmp.crc16(h->crc16());
    tmp.filetime(h->get_fileinfo_date(), h->get_fileinfo_ns());

    if(tmp.filter(filter, filter_flags))
      list.push_back(std::move(tmp));
  }
  while((h = h->next_header(entry_start, entry_end)));

  char path_buf[1024];
  for(ArcFS_entry *dir : dirs)
  {
    h = dir->subdirectory_header(entry_start, entry_end);
    if(!h)
      continue;

    if(base && base[0])
    {
      snprintf(path_buf, sizeof(path_buf), "%s%c%s", base, DIR_SEPARATOR, dir->filename());
    }
    else
      snprintf(path_buf, sizeof(path_buf), "%s", dir->filename());

    search_r(list, filter, filter_flags, path_buf, recursive, h);
  }
}

bool ArcFSImage::unpack_file(const FileInfo &file, uint8_t **dest, size_t *dest_len, uint16_t *dest_crc)
{
  ArcFS_entry *h = reinterpret_cast<ArcFS_entry *>(file.priv);

  /* Can't unpack directories. */
  if(~file.get_type() & FileInfo::IS_REG)
    return false;

  // Verify data pointer is OK...
  if(h->data_offset() >= data_length || header.data_offset() >= data_length)
    return false;

  if(h->data_offset() > data_length - header.data_offset())
    return false;

  uint8_t *input = data + h->data_offset() + header.data_offset();
  size_t input_size = h->compressed_size();

  uint8_t *output;
  size_t output_size;

  ArcFS_type type = h->type();

  if(type != UNPACKED)
  {
    output_size = h->uncompressed_size();
    output = new uint8_t[output_size];
    held_buffer.reset(output);

    const char *err = arc_unpack(output, output_size, input, input_size, type, h->compression_bits());
    if(err)
    {
      format::error("%s (%u)", err, type);
      return false;
    }
  }
  else
  {
    output = input;
    output_size = input_size;
  }

  // Seems like some ArcFS archives have the CRC-16s as all 0s. Just ignore those...
  if(h->crc16())
  {
    *dest_crc = dimgutil_crc16_IBM(0, output, output_size);
    if(*dest_crc != h->crc16())
      format::warning("CRC-16 mismatch: expected 0x%04x, got 0x%04x", h->crc16(), *dest_crc);
  }
  else
    *dest_crc = 0;

  *dest = output;
  *dest_len = output_size;
  return true;
}

bool ArcFSImage::Test(const FileInfo &file)
{
  ArcFS_entry *h = reinterpret_cast<ArcFS_entry *>(file.priv);
  uint8_t *output;
  size_t output_size;
  uint16_t output_crc;

  if(file.get_type() & FileInfo::IS_DIRECTORY)
  {
    // todo probably technically possible to verify these if there's a CRC?
    return true;
  }

  if(unpack_file(file, &output, &output_size, &output_crc))
    return output_crc == h->crc16();

  return false;
}

bool ArcFSImage::Extract(const FileInfo &file, const char *destdir)
{
  uint8_t *output;
  size_t output_size;
  uint16_t output_crc;

  if(file.get_type() & FileInfo::IS_DIRECTORY)
  {
    if(!FileIO::create_directory(file.name(), destdir))
    {
      format::error("failed mkdir");
      return false;
    }
    return true;
  }
  else

  if(file.get_type() & FileInfo::IS_REG)
  {
    if(!unpack_file(file, &output, &output_size, &output_crc))
      return false;

    FileIO output_file;
    FILE *fp = output_file.get_file();
    if(!fp)
      return false;

    if(fwrite(output, 1, output_size, fp) != output_size)
      return false;

    return output_file.commit(file, destdir);
  }
  return false;
}

ArcFS_entry *ArcFSImage::get_entry(const char *path) const
{
  char buffer[1024];
  ArcFS_entry *h = entry_start;

  snprintf(buffer, sizeof(buffer), "%s", path);
  path_clean_slashes(buffer);

  char *cursor = buffer;
  char *current;
  while((current = path_tokenize(&cursor)))
  {
    do
    {
      if(h >= entry_end)
        return nullptr;

      if(h->type() == ARCFS_INVALID)
        return nullptr;

      if(h->type() == DELETED)
        continue;

      if(!strcasecmp(current, h->filename()))
        break;
    }
    while((h = h->next_header(entry_start, entry_end)));

    if(!h)
      return nullptr;

    if(cursor)
    {
      h = h->subdirectory_header(entry_start, entry_end);
      if(!h)
        return nullptr;
    }
  }
  return h;
}


class ArcFSLoader: public DiskImageLoader
{
public:
  virtual DiskImage *Load(FILE *fp, long file_length) const override
  {
    ArcFS_header h{};

    if(!fread(h.data, sizeof(h.data), 1, fp))
      return nullptr;

    if(!h.is_valid())
      return nullptr;

    rewind(fp);
    return new ArcFSImage(h, fp, file_length);
  }
};

static const ArcFSLoader loader;
