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
#include "FileIO.hpp"
#include "arc_crc16.h"
#include "arc_unpack.h"

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

  static constexpr int ARC_HEADER_1_SIZE = 25;
  static constexpr int ARC_HEADER_SIZE = 29;
  static constexpr int SPARK_HEADER_1_SIZE = ARC_HEADER_1_SIZE + 12;
  static constexpr int SPARK_HEADER_SIZE = ARC_HEADER_SIZE + 12;

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

  bool is_spark() const
  {
    if(data[1] >= SPARK_END_OF_ARCHIVE)
      return true;
    return false;
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
    // Type 1 doesn't store a separate uncompressed size field.
    if(data[1] == UNPACKED_OLD || data[1] == SPARK_UNPACKED_OLD)
      return mem_u32le(data + 15);
    return mem_u32le(data + 25);
  }

  /* TODO: attributes. */

  bool read_header(FILE *fp)
  {
    if(!fread(data, ARC_HEADER_SIZE, 1, fp))
      return false;

    if(is_spark())
      if(!fread(data + ARC_HEADER_SIZE, SPARK_HEADER_SIZE - ARC_HEADER_SIZE, 1, fp))
        return false;

    // Make sure filename is terminated...
    data[14] = '\0';
    return true;
  }

  int get_header_size() const
  {
    if(data[1] == UNPACKED_OLD)
      return ARC_HEADER_1_SIZE;
    if(data[1] == SPARK_UNPACKED_OLD)
      return SPARK_HEADER_1_SIZE;
    if(is_spark())
      return SPARK_HEADER_SIZE;
    return ARC_HEADER_SIZE;
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
    ptrdiff_t left = data_end - data;
    if(data < data_start || data >= data_end)
      return nullptr;

    ARC_type t = type();
    if(t == ARC_INVALID || t == END_OF_ARCHIVE || t == SPARK_END_OF_ARCHIVE)
      return nullptr;

    ptrdiff_t header_size = get_header_size();
    ptrdiff_t offset = compressed_size() + header_size;

    if(offset > left || header_size > left - offset)
      return nullptr;

    ARC_entry *ret = reinterpret_cast<ARC_entry *>(data + offset);
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

  bool get_buffer(const uint8_t **dest, size_t *dest_length) const
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
        return FileInfo::IS_REG;
    }
  }


  /**
   * Determine if a block of memory represents an ARC/Spark archive.
   * `buffer_start` generally should be this archive + get_header_size()
   * and `buffer_length` generally should be `uncompressed_size()`.
   */
  static bool is_valid_arc(uint8_t *buffer_start, size_t buffer_length)
  {
    if(buffer_length < ARC_HEADER_SIZE)
      return false;

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
  virtual bool Extract(const FileInfo &file, const char *destdir = nullptr) const override;

  void search_r(FileList &list, const FileInfo &filter, uint32_t filter_flags,
   const char *base, bool recursive, ARC_entry *h, uint8_t *start, size_t length) const;

  ARC_entry *get_entry(const char *path, uint8_t **start, size_t *length) const;
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

  ARC_entry *h = reinterpret_cast<ARC_entry *>(data);
  uint8_t *start = data;
  size_t length = data_length;

  if(base && base[0])
  {
    uint8_t *_start;
    size_t _length;
    h = get_entry(base, &start, &length);
    if(!h || !h->get_buffer(&_start, &_length))
      return false;

    ARC_entry *_h = reinterpret_cast<ARC_entry *>(_start);
    if(!_length || !_h->is_valid())
    {
      // Base is file.
      FileInfo tmp("", base, h->get_filetype(false), h->uncompressed_size(), h->compressed_size());
      tmp.priv = h;
      tmp.filetime(FileInfo::convert_DOS(h->dos_date(), h->dos_time()));
      if(tmp.filter(filter, filter_flags))
        list.push_back(std::move(tmp));

      return true;
    }

    // Base is directory.
    h = _h;
    start = _start;
    length = _length;
  }

  search_r(list, filter, filter_flags, base, recursive, h, start, length);
  return true;
}

void SparkImage::search_r(FileList &list, const FileInfo &filter, uint32_t filter_flags,
 const char *base, bool recursive, ARC_entry *h, uint8_t *start, size_t length) const
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

    FileInfo tmp(base, h->filename(), h->get_filetype(is_dir), h->uncompressed_size(), h->compressed_size());
    tmp.priv = h;
    tmp.filetime(FileInfo::convert_DOS(h->dos_date(), h->dos_time()));

    if(tmp.filter(filter, filter_flags))
      list.push_back(std::move(tmp));
  }
  while((h = h->next_header(start, length)));

  char path_buf[1024];
  for(ARC_entry *dir : dirs)
  {
    if(dir->get_buffer(&dir_buf, &dir_length))
    {
      h = reinterpret_cast<ARC_entry *>(dir_buf);

      if(base && base[0])
      {
        snprintf(path_buf, sizeof(path_buf), "%s%c%s", base, DIR_SEPARATOR, dir->filename());
      }
      else
        snprintf(path_buf, sizeof(path_buf), "%s", dir->filename());

      search_r(list, filter, filter_flags, path_buf, recursive, h, dir_buf, dir_length);
    }
  }
}

bool SparkImage::Extract(const FileInfo &file, const char *destdir) const
{
  const ARC_entry *h = reinterpret_cast<const ARC_entry *>(file.priv);

  if(file.get_type() & FileInfo::IS_DIRECTORY)
  {
    // FIXME destdir!
    if(!FileIO::io_mkdir_recursive(file.name()))
    {
      format::error("failed mkdir");
      return false;
    }
  }
  else

  if(file.get_type() & FileInfo::IS_REG)
  {
    FileIO output_file;
    FILE *fp = output_file.get_file();
    if(!fp)
      return false;

    const uint8_t *input;
    size_t input_size;
    if(!h->get_buffer(&input, &input_size))
      return false;

    uint8_t *output;
    size_t output_size;

    ARC_type type = h->type();

    std::unique_ptr<uint8_t> _free_me(nullptr);
    if(type != UNPACKED_OLD && type != UNPACKED)
    {
      output_size = h->uncompressed_size();
      output = new uint8_t[output_size];
      _free_me.reset(output);
    }

    switch(type)
    {
      case UNPACKED_OLD:
      case UNPACKED:
        output = const_cast<uint8_t *>(input);
        output_size = input_size;
        break;

      case PACKED:
        if(arc_unpack_rle90(output, output_size, input, input_size) < 0)
        {
          format::error("failed to unpack file");
          return false;
        }
        break;

      default:
        fprintf(stderr, "  ERROR: unsupported compression type %u\n", type);
        return false;
    }

    uint16_t crc16 = arc_crc16(output, output_size);
    if(crc16 != h->crc16())
      format::warning("CRC-16 mismatch: expected 0x%04x, got 0x%04x", h->crc16(), crc16);

    if(fwrite(output, 1, output_size, fp) != output_size)
      return false;

    return output_file.commit(file, destdir);
  }

  return true;
}

ARC_entry *SparkImage::get_entry(const char *path, uint8_t **start, size_t *length) const
{
  char buffer[1024];
  uint8_t *h_buf = data;
  size_t h_length = data_length;
  ARC_entry *h;

  snprintf(buffer, sizeof(buffer), "%s", path);
  path_clean_slashes(buffer);

  char *cursor = buffer;
  char *current;
  while((current = path_tokenize(&cursor)))
  {
    h = reinterpret_cast<ARC_entry *>(h_buf);
    if(h->type() == ARC_INVALID)
      return nullptr;

    do
    {
      if(!strcasecmp(current, h->filename()))
        break;
    }
    while((h = h->next_header(h_buf, h_length)));

    if(!h)
      return nullptr;

    if(cursor)
    {
      if(!h->get_buffer(&h_buf, &h_length))
        return nullptr;
    }
  }
  *start = h_buf;
  *length = h_length;
  return h;
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

      if(h.is_spark())
        is_spark = true;
    }
    while(h.next_header(fp));
    rewind(fp);

    return new SparkImage(is_spark, count, fp, file_length);
  }
};

static const SparkLoader loader;
