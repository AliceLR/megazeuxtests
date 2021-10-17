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

/**
 * Unpacker for ARC/ArcFS/Spark archives.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "DiskImage.hpp"
#include "../format.hpp"


enum ARC_type
{
  END_OF_ARCHIVE,
  UNPACKED_OLD,
  UNPACKED,
  PACKED,
  SQUEEZED,
  CRUNCHED_5,
  CRUNCHED_6,
  CRUNCHED_7,
  CRUNCHED,
  SQUASHED,

  ARCHIVE_INFO = 20,
  FILE_INFO    = 21,
  OS_INFO      = 22,

  SPARK_END_OF_ARCHIVE = 0x80,
  SPARK_UNPACKED_OLD   = 0x81,
  SPARK_UNPACKED       = 0x82,
  SPARK_PACKED         = 0x83,
  SPARK_CRUNCHED       = 0x88,
  SPARK_SQUASHED       = 0x89,
  SPARK_COMPRESSED     = 0xff,

  ARC_INVALID = -1,
};

struct ARC_entry
{
  /*  0 */ //uint8_t  magic; /* 0x1a */
  /*  1 */ //uint8_t  type;
  /*  2 */ //char     filename[13];
  /* 15 */ //uint32_t compressed_size;
  /* 19 */ //uint16_t dos_date;
  /* 21 */ //uint16_t dos_time;
  /* 23 */ //uint16_t crc16;
  /* 25 */ //uint32_t uncompressed_size;
  /* 29 */

  /* Spark only. */
  /* 29 */ //uint8_t  attributes[12];
  /* 41 */

  uint8_t data[41];

  bool is_valid() const
  {
    return data[0] == 0x1a && type() != ARC_INVALID;
  }

  ARC_type type() const
  {
    switch(data[1])
    {
      case END_OF_ARCHIVE:
      case UNPACKED_OLD:
      case UNPACKED:
      case PACKED:
      case SQUEEZED:
      case CRUNCHED_5:
      case CRUNCHED_6:
      case CRUNCHED_7:
      case CRUNCHED:
      case SQUASHED:
      case ARCHIVE_INFO:
      case FILE_INFO:
      case OS_INFO:
      case SPARK_END_OF_ARCHIVE:
      case SPARK_UNPACKED_OLD:
      case SPARK_UNPACKED:
      case SPARK_PACKED:
      case SPARK_CRUNCHED:
      case SPARK_SQUASHED:
        return static_cast<ARC_type>(data[1] & 0x7f); // & 0x7f to convert Spark types to normal.

      case SPARK_COMPRESSED:
        return SPARK_COMPRESSED; // Leave as 255.
    }
    return ARC_INVALID;
  }

  const char *filename() const
  {
    return reinterpret_cast<const char *>(data + 2);
  }

  uint32_t compressed_size() const
  {
    return mem_u32le(data + 15);
  }

  /* Some places erroneously claim the timestamp is a single little endian u32 with date in the high bytes. */
  uint16_t dos_date() const
  {
    return mem_u16le(data + 19);
  }

  uint16_t dos_time() const
  {
    return mem_u16le(data + 21);
  }

  uint16_t crc16() const
  {
    return mem_u16le(data + 23);
  }

  uint32_t uncompressed_size() const
  {
    return mem_u32le(data + 25);
  }

  /* TODO: attributes. */

  bool read_header(FILE *fp)
  {
    if(!fread(data, 29, 1, fp))
      return false;

    if(data[1] >= SPARK_END_OF_ARCHIVE)
      if(!fread(data + 29, 12, 1, fp))
        return false;

    // Make sure filename is terminated...
    data[14] = '\0';
    return true;
  }

  int get_header_size() const
  {
    if(static_cast<ARC_type>(data[1]) >= SPARK_END_OF_ARCHIVE)
      return 41;
    return 29;
  }

  /**
   * Get the next header for an ARC_entry that exists in a stream.
   * The returned ARC_entry * will be a pointer to this entry, and the
   * data in this entry will be overwritten with the next entry.
   */
  ARC_entry *next_header(FILE *fp)
  {
    ARC_type t = type();
    if(t == ARC_INVALID || t == END_OF_ARCHIVE || t == SPARK_END_OF_ARCHIVE)
      return nullptr;

    if(fseek(fp, compressed_size(), SEEK_CUR))
      return nullptr;

    if(!read_header(fp))
      return nullptr;

    return this;
  }

  /**
   * Get the next header for an ARC_entry that exists in a buffer in RAM.
   * The returned ARC_entry * will be a pointer to the next ARC_entry in
   * that buffer.
   */
  ARC_entry *next_header(uint8_t *data_start, size_t data_length)
  {
    uint8_t *data_end = data_start + data_length;
    uint8_t *current = reinterpret_cast<uint8_t *>(this);
    if(current < data_start || current >= data_end)
      return nullptr;

    ARC_type t = type();
    if(t == ARC_INVALID || t == END_OF_ARCHIVE || t == SPARK_END_OF_ARCHIVE)
      return nullptr;

    ptrdiff_t header_size = get_header_size();
    ptrdiff_t offset = compressed_size() + header_size;

    if(offset > data_end - current || header_size > data_end - current - offset)
      return nullptr;

    ARC_entry *ret = reinterpret_cast<ARC_entry *>(current + offset);
    ret->data[14] = '\0';
    return ret;
  }

  /**
   * Only use on headers stored in continuous archive memory pls :-(
   */
  bool get_buffer(uint8_t **dest, size_t *dest_length)
  {
    if(!is_valid())
      return false;

    *dest = data + get_header_size();
    *dest_length = compressed_size();
    return true;
  }

  int get_filetype(bool is_dir) const
  {
    if(is_dir)
      return FileInfo::IS_DIRECTORY;
    switch(type())
    {
      case ARCHIVE_INFO:
      case FILE_INFO:
      case OS_INFO:
        return FileInfo::IS_INFO;
      default:
        return 0;
    }
  }


  /**
   * Determine if a block of memory represents an ARC/Spark archive.
   * `buffer_start` generally should be this archive + get_header_size()
   * and `buffer_length` generally should be `uncompressed_size()`.
   */
  static bool is_valid_arc(uint8_t *buffer_start, size_t buffer_length)
  {
    ARC_entry *h = reinterpret_cast<ARC_entry *>(buffer_start);
    do
    {
      if(!h->is_valid())
        return false;
    }
    while((h = h->next_header(buffer_start, buffer_length)));
    return true;
  }

  static bool is_info_type(ARC_type t)
  {
    switch(t)
    {
      case ARCHIVE_INFO:
      case FILE_INFO:
      case OS_INFO:
        return true;
      default:
        return false;
    }
  }
};



class SparkImage: public DiskImage
{
  uint8_t *data = nullptr;
  size_t data_length;
  size_t num_files;

public:
  SparkImage(bool is_spark, size_t _num_files, FILE *fp, long file_length):
   DiskImage::DiskImage(is_spark ? "Spark" : "ARC", "Archive"), data_length(file_length), num_files(_num_files)
  {
    data = new uint8_t[file_length];
    if(!fread(data, file_length, 1, fp))
      error_state = true;
  }
  virtual ~SparkImage()
  {
    delete[] data;
  }

  /* "Driver" implemented functions. */
  virtual bool PrintSummary() const override;
  virtual bool Search(FileList &list, const FileInfo &filter, uint32_t filter_flags,
   const char *base, bool recursive = false) const override;

  void search_r(FileList &list, const FileInfo &filter, uint32_t filter_flags,
   const char *base, bool recursive, ARC_entry *h) const;
};

bool SparkImage::PrintSummary() const
{
  if(error_state)
    return false;

  format::line("Type",     "%s", type);
  format::line("Media",    "%s", media);
  format::line("Size",     "%zu", data_length);
  format::line("Files",    "%zu", num_files);
  return true;
}

bool SparkImage::Search(FileList &list, const FileInfo &filter, uint32_t filter_flags,
 const char *base, bool recursive) const
{
  if(error_state)
    return false;

  ARC_entry *first = reinterpret_cast<ARC_entry *>(data);

  search_r(list, filter, filter_flags, base, recursive, first);
  return true;
}

void SparkImage::search_r(FileList &list, const FileInfo &filter, uint32_t filter_flags,
 const char *base, bool recursive, ARC_entry *h) const
{
  std::vector<ARC_entry *> dirs;
  uint8_t *dir_buf;
  size_t dir_length;

  do
  {
    // It might be possible for directories to be compressed, but detection would
    // require partially unpacking them preemptively. Only scan uncompressed nested archives!
    bool is_dir = false;
    if(h->is_valid() && (h->type() == UNPACKED || h->type() == UNPACKED_OLD))
    {
      if(h->get_buffer(&dir_buf, &dir_length) && ARC_entry::is_valid_arc(dir_buf, dir_length))
      {
        is_dir = true;
        if(recursive)
          dirs.push_back(h);
      }
    }

    FileInfo tmp(base, h->filename(), h->get_filetype(is_dir), h->uncompressed_size());
    tmp.priv = h;
    tmp.filetime(FileInfo::convert_DOS(h->dos_date(), h->dos_time()));

    if(tmp.filter(filter, filter_flags))
      list.push_back(std::move(tmp));
  }
  while((h = h->next_header(data, data_length)));

  for(ARC_entry *dir : dirs)
  {
    if(dir->get_buffer(&dir_buf, &dir_length))
    {
      ARC_entry *d = reinterpret_cast<ARC_entry *>(dir_buf);

      search_r(list, filter, filter_flags, base, recursive, d);
    }
  }
}


class SparkLoader: public DiskImageLoader
{
public:
  virtual DiskImage *Load(FILE *fp, long file_length) const override
  {
    ARC_entry h{};
    if(file_length <= 0 || !h.read_header(fp))
      return nullptr;

    bool is_spark = false;
    int count = 0;
    do
    {
      count++;
      if(!h.is_valid())
        return nullptr;

      if(h.type() >= SPARK_END_OF_ARCHIVE)
        is_spark = true;
    }
    while(h.next_header(fp));
    rewind(fp);

    return new SparkImage(is_spark, count, fp, file_length);
  }
};

static const SparkLoader loader;
