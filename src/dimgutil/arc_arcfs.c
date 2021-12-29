/**
 * dimgutil: disk image and archive utility
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
 * Simple single-pass stdout unpacker for ArcFS archives.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arc_types.h"
#include "arc_crc16.h"
#include "arc_unpack.h"

/* Arbitrary maximum allowed output filesize. */
#define ARCFS_MAX_OUTPUT (1 << 28)

/* #define ARCFS_DEBUG */

#define ARCFS_HEADER_SIZE 96
#define ARCFS_ENTRY_SIZE 36

#define ARCFS_END_OF_DIR 0
#define ARCFS_DELETED 1

#ifdef ARCFS_DEBUG
#define debug(...) do{ fprintf(stderr, "" __VA_ARGS__); fflush(stderr); }while(0)
#endif

static arc_uint16 arc_mem_u16(arc_uint8 *buf)
{
  return (buf[1] << 8) | buf[0];
}

static arc_uint32 arc_mem_u32(arc_uint8 *buf)
{
  return (buf[3] << 24UL) | (buf[2] << 16UL) | (buf[1] << 8UL) | buf[0];
}

struct arcfs_data
{
  /*  0    char magic[8]; */
  /*  8 */ arc_uint32 entries_length;
  /* 12 */ arc_uint32 data_offset;
  /* 16 */ arc_uint32 min_read_version;
  /* 20 */ arc_uint32 min_write_version;
  /* 24 */ arc_uint32 format_version;
  /* 28    Filler. */
  /* 96 */
};

struct arcfs_entry
{
  /*  0 */ arc_uint8 method;
  /*  1 */ char filename[12];
  /* 12 */ arc_uint32 uncompressed_size;
  /* 16    arc_uint32 load_offset; */
  /* 20    arc_uint32 exec_offset; */
  /* 24    arc_uint32 attributes; */
  /* 28 */ arc_uint32 compressed_size;
  /* 32    arc_uint32 info; */
  /* 36 */

  /* Unpacked fields */
  arc_uint16 crc16;
  arc_uint8  compression_bits;
  arc_uint8  is_directory;
  arc_uint32 value_offset;
};

static int arcfs_check_magic(const unsigned char *buf)
{
  return memcmp(buf, "Archive\x00", 8) ? -1 : 0;
}

static int arcfs_read_header(struct arcfs_data *data, FILE *f)
{
  arc_uint8 buffer[ARCFS_HEADER_SIZE];

  if(fread(buffer, 1, ARCFS_HEADER_SIZE, f) < ARCFS_HEADER_SIZE)
  {
#ifdef ARCFS_DEBUG
    debug("short read in header\n");
#endif
    return -1;
  }

  if(arcfs_check_magic(buffer) < 0)
  {
#ifdef ARCFS_DEBUG
    debug("bad header magic: %8.8s\n", (char *)buffer);
#endif
    return -1;
  }

  data->entries_length    = arc_mem_u32(buffer + 8);
  data->data_offset       = arc_mem_u32(buffer + 12);
  data->min_read_version  = arc_mem_u32(buffer + 16);
  data->min_write_version = arc_mem_u32(buffer + 20);
  data->format_version    = arc_mem_u32(buffer + 24);

  if(data->entries_length % ARCFS_ENTRY_SIZE != 0)
  {
#ifdef ARCFS_DEBUG
    debug("bad entries length: %zu\n", (size_t)data->entries_length);
#endif
    return -1;
  }

  if(data->data_offset < ARCFS_HEADER_SIZE ||
     data->data_offset - ARCFS_HEADER_SIZE < data->entries_length)
  {
#ifdef ARCFS_DEBUG
    debug("bad data offset: %zu\n", (size_t)data->data_offset);
#endif
    return -1;
  }

  /* These seem to be the highest versions that exist. */
  if(data->min_read_version > 260 || data->min_write_version > 260 || data->format_version > 0x0a)
  {
#ifdef ARCFS_DEBUG
    debug("bad versions: %zu %zu %zu\n", (size_t)data->min_read_version,
     (size_t)data->min_write_version, (size_t)data->format_version);
#endif
    return -1;
  }

  return 0;
}

static int arcfs_read_entry(struct arcfs_entry *e, FILE *f)
{
  arc_uint8 buffer[ARCFS_ENTRY_SIZE];

  if(fread(buffer, 1, ARCFS_ENTRY_SIZE, f) < ARCFS_ENTRY_SIZE)
    return -1;

  e->method = buffer[0] & 0x7f;
  if(e->method == ARCFS_END_OF_DIR)
    return 0;

  memcpy(e->filename, buffer + 1, 11);
  e->filename[11] = '\0';

  e->uncompressed_size = arc_mem_u32(buffer + 12);
  e->compression_bits  = buffer[25]; /* attributes */
  e->crc16             = arc_mem_u16(buffer + 26); /* attributes */
  e->compressed_size   = arc_mem_u32(buffer + 28);
  e->value_offset      = arc_mem_u32(buffer + 32) & 0x7fffffffUL; /* info */
  e->is_directory      = buffer[35] >> 7; /* info */

  return 0;
}

static int arcfs_read(unsigned char **dest, size_t *dest_len, FILE *f, size_t file_len)
{
  struct arcfs_data data;
  struct arcfs_entry e;
  size_t offset;
  size_t i;
  unsigned char *in;
  unsigned char *out;
  size_t out_len;

  if(arcfs_read_header(&data, f) < 0)
    return -1;

  if(data.data_offset > file_len)
    return -1;

  for(i = 0; i < data.entries_length; i += ARCFS_ENTRY_SIZE)
  {
    if(arcfs_read_entry(&e, f) < 0)
    {
#ifdef ARCFS_DEBUG
      debug("error reading entry %zu\n", (size_t)data.entries_length / ARCFS_ENTRY_SIZE);
#endif
      return -1;
    }

#ifdef ARCFS_DEBUG
    debug("checking file: %s\n", e.filename);
#endif

    /* Ignore directories, end of directory markers, deleted files. */
    if(e.is_directory || e.method == ARCFS_END_OF_DIR || e.method == ARCFS_DELETED)
      continue;

    if(e.method == ARC_M_UNPACKED && e.compressed_size != e.uncompressed_size)
      e.compressed_size = e.uncompressed_size;

    /* Ignore junk offset/size. */
    if(e.value_offset >= file_len - data.data_offset)
      continue;

    offset = data.data_offset + e.value_offset;
    if(e.compressed_size > file_len - offset)
      continue;

    /* Ignore sizes over the allowed limit. */
    if(e.uncompressed_size > ARCFS_MAX_OUTPUT)
      continue;

    /* Ignore unsupported methods. */
    if(arc_method_is_supported(e.method) < 0)
      continue;

    /* Read file. */
#ifdef ARCFS_DEBUG
    debug("unpacking file: %s\n", e.filename);
#endif

    if(fseek(f, offset, SEEK_SET) < 0)
      return -1;

    in = (unsigned char *)malloc(e.compressed_size);
    if(!in)
      return -1;

    if(fread(in, 1, e.compressed_size, f) < e.compressed_size)
    {
      free(in);
      return -1;
    }

    if(e.method != ARC_M_UNPACKED)
    {
      out = (unsigned char *)malloc(e.uncompressed_size);
      out_len = e.uncompressed_size;

      if(!out)
      {
        free(in);
        return -1;
      }

      if(arc_unpack(out, out_len, in, e.compressed_size, e.method, e.compression_bits) != NULL)
      {
        free(in);
        free(out);
        return -1;
      }
      free(in);
    }
    else
    {
      out = in;
      out_len = e.uncompressed_size;
    }

    /* ArcFS CRC may sometimes just be 0, in which case, ignore it. */
    if(e.crc16)
    {
      arc_uint16 dest_crc16 = arc_crc16(out, out_len);
      if(e.crc16 != dest_crc16)
      {
#ifdef ARCFS_DEBUG
        debug("crc16 mismatch: expected %u, got %u\n", e.crc16, dest_crc16);
#endif
        free(out);
        return -1;
      }
    }
    *dest = out;
    *dest_len = out_len;
    return 0;
  }
  return -1;
}


#ifdef _WIN32
#include <fcntl.h>
#endif

int main(int argc, char *argv[])
{
  FILE *f;
  size_t file_length;
  unsigned char *data;
  size_t data_length;

  if(argc < 2)
    return -1;

#ifdef _WIN32
    /* Windows forces stdout to be text mode by default, fix it. */
    _setmode(_fileno(stdout), _O_BINARY);
#endif

  f = fopen(argv[1], "rb");
  if(!f)
    return -1;

  fseek(f, 0, SEEK_END);
  file_length = ftell(f);
  rewind(f);

  if(arcfs_read(&data, &data_length, f, file_length) < 0)
    return -1;

  fwrite(data, data_length, 1, stdout);
  free(data);
  return 0;
}
