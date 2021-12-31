/**
 * dimgutil: disk image and archive utility
 * Copyright (C) 2021 Alice Rowan <petrifiedrowan@gmail.com>
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
 * Simple single-file unpacker for ARC/Spark archives.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arc_types.h"
#include "arc_crc16.h"
#include "arc_unpack.h"

/* Arbitrary maximum allowed output filesize. */
#define ARC_MAX_OUTPUT (1 << 28)

/* #define ARC_DEBUG */

#define ARC_HEADER_SIZE    29
#define SPARK_HEADER_EXTRA 12

#define ARC_END_OF_ARCHIVE 0
#define ARC_6_DIR          30
#define ARC_6_END_OF_DIR   31

#ifdef ARC_DEBUG
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

struct arc_entry
{
  /*  0    arc_uint8  magic; */ /* 0x1a */
  /*  1 */ arc_uint8  method;
  /*  2 */ char       filename[13];
  /* 15 */ arc_uint32 compressed_size;
  /* 19    arc_uint16 dos_date; */ /* Same as ZIP. */
  /* 21    arc_uint16 dos_time; */ /* Same as ZIP. */
  /* 23 */ arc_uint16 crc16;
  /* 25 */ arc_uint32 uncompressed_size; /* Note: method 1 omits this field. */
  /* 29 */

  /* Spark only. */
  /* load_address and exec_address encode the filetype and RISC OS timestamp
   * if the top 12 bits of load_address are 0xFFF. */
  /* 29 */ arc_uint32 load_address;
  /* 33    arc_uint32 exec_address; */
  /* 37    arc_uint32 attributes; */
  /* 41 */
};

static inline int is_arc_archive(unsigned char *buf)
{
  int i;

  /* Test magic. */
  if(buf[0] != 0x1a)
    return 0;

  /* Test filename for garbage and missing terminator. */
  for(i = 0; i < 13; i++)
  {
    if(buf[i + 2] == '\0')
      break;
    if(buf[i + 2] < 32 || buf[i + 2] == 0x7f)
      return 0;
  }
  if(i >= 13)
    return 0;

  /* Test type. Not guaranteed to be a complete list. */
  switch(buf[1])
  {
    /* ARC types. */
    case ARC_END_OF_ARCHIVE:
    case ARC_M_UNPACKED_OLD:
    case ARC_M_UNPACKED:
    case ARC_M_PACKED:
    case ARC_M_SQUEEZED:
    case ARC_M_CRUNCHED_5:
    case ARC_M_CRUNCHED_6:
    case ARC_M_CRUNCHED_7:
    case ARC_M_CRUNCHED:
    case ARC_M_SQUASHED:
    case ARC_M_TRIMMED: /* Also PAK crushed */
    case 11: /* PAK distilled */
    case 20: /* archive info */
    case 21: /* extended file info */
    case 22: /* OS-specific info */
    case ARC_6_DIR:
    case ARC_6_END_OF_DIR:
      return 1;
  }
  switch((int)buf[1] - 0x80)
  {
    /* Spark types. */
    case ARC_END_OF_ARCHIVE:
    case ARC_M_UNPACKED_OLD:
    case ARC_M_UNPACKED:
    case ARC_M_PACKED:
    case ARC_M_SQUEEZED:
    case ARC_M_CRUNCHED_5:
    case ARC_M_CRUNCHED_6:
    case ARC_M_CRUNCHED_7:
    case ARC_M_CRUNCHED:
    case ARC_M_SQUASHED:
    case ARC_M_COMPRESSED:
      return 1;
  }
  return 0;
}

static int is_packed(int method)
{
  method &= 0x7f;
  if(method == ARC_M_UNPACKED || method == ARC_M_UNPACKED_OLD)
    return 0;
  return 1;
}

static int is_spark(int method)
{
  return method & 0x80;
}

static int is_directory(struct arc_entry *e)
{
  /* ARC 6 directories have a dedicated type. */
  if(e->method == ARC_6_DIR)
    return 1;

  /* Spark directories are never packed and have the Spark type bit set. */
  if(e->method != (0x80 | (int)ARC_M_UNPACKED))
    return 0;

  /* Spark: top 12 bits must be 0xfff and filetype must be 0xddc (RISC OS archive). */
  if(e->load_address >> 8 != 0xfffddcUL)
    return 0;

  return 1;
}

static size_t arc_header_length(int method)
{
  size_t len = ARC_HEADER_SIZE;

  /* End-of-archive and end-of-directory should be only 2 bytes long.
   * Spark subdirectories end with end-of-archive, not end-of-directory. */
  if((method & 0x7f) == ARC_END_OF_ARCHIVE || method == ARC_6_END_OF_DIR)
    return 2;

  if((method & 0x7f) == ARC_M_UNPACKED_OLD)
    len -= 4;
  if(is_spark(method))
    len += SPARK_HEADER_EXTRA;
  return len;
}

static int arc_read_entry(struct arc_entry *e, FILE *f)
{
  arc_uint8 buf[ARC_HEADER_SIZE + SPARK_HEADER_EXTRA];
  size_t header_len;

  if(fread(buf, 1, 2, f) < 2 || buf[0] != 0x1a)
    return -1;

  e->method = buf[1];
  header_len = arc_header_length(e->method);
  if(header_len <= 2)
    return 0;

  if(fread(buf + 2, 1, header_len - 2, f) < header_len - 2)
    return -1;

  memcpy(e->filename, buf + 2, 12);
  e->filename[12] = '\0';

  e->compressed_size = arc_mem_u32(buf + 15);
  e->crc16 = arc_mem_u16(buf + 23);

  if(e->method == ARC_M_UNPACKED_OLD)
    e->uncompressed_size = e->compressed_size;
  else
    e->uncompressed_size = arc_mem_u32(buf + 25);

  if(is_spark(e->method))
  {
    /* Spark stores extra RISC OS attribute information. */
    size_t offset = header_len - SPARK_HEADER_EXTRA;
    e->load_address = arc_mem_u32(buf + offset);
  }
  return 0;
}

static int arc_read(unsigned char **dest, size_t *dest_len, FILE *f, unsigned long file_len)
{
  struct arc_entry e;
  unsigned char *in;
  unsigned char *out;
  const char *err;
  size_t out_len;
  int level = 0;
  arc_uint16 out_crc16;

  while(1)
  {
    if(arc_read_entry(&e, f) < 0)
    {
#ifdef ARC_DEBUG
      debug("failed to read ARC entry\n");
#endif
      return -1;
    }

    if((e.method & 0x7f) == ARC_END_OF_ARCHIVE || e.method == ARC_6_END_OF_DIR)
    {
      if(level > 0)
      {
        /* Valid directories can be continued out of directly into the following
         * parent directory files. Note: manually nested archives where the inner
         * archive has trailing data may end up erroring due to this simple handling. */
#ifdef ARC_DEBUG
        debug("exiting directory\n");
#endif
        level--;
        continue;
      }
      return -1;
    }

    /* Special: both ARC 6 and Spark directories are stored as nested archives.
     * The contents of these can just be read as if they're part of the parent. */
    if(is_directory(&e))
    {
#ifdef ARC_DEBUG
      debug("entering directory: %s\n", e.filename);
#endif
      level++;
      continue;
    }

    if(e.method == ARC_M_UNPACKED)
      e.uncompressed_size = e.compressed_size;

    /* Skip unknown types, junk compressed sizes, and unsupported uncompressed sizes. */
    if(arc_method_is_supported(e.method) < 0 ||
       e.compressed_size > file_len ||
       e.uncompressed_size > ARC_MAX_OUTPUT)
    {
#ifdef ARC_DEBUG
      debug("skipping: method=%d compr=%zu uncompr=%zu\n",
       e.method, (size_t)e.compressed_size, (size_t)e.uncompressed_size);
#endif
      if(fseek(f, e.compressed_size, SEEK_CUR) < 0)
        return -1;

      continue;
    }

#ifdef ARC_DEBUG
    debug("file: %s\n", e.filename);
#endif

    /* Attempt to unpack. */
    in = (unsigned char *)malloc(e.compressed_size);
    if(!in)
      return -1;

    if(fread(in, 1, e.compressed_size, f) < e.compressed_size)
    {
      free(in);
      return -1;
    }

    if(is_packed(e.method))
    {
      out = (unsigned char *)malloc(e.uncompressed_size);
      out_len = e.uncompressed_size;
      if(!out)
      {
        free(in);
        return -1;
      }

      err = arc_unpack(out, out_len, in, e.compressed_size, e.method, 0);
      if(err != NULL)
      {
#ifdef ARC_DEBUG
        debug("error unpacking: %s\n", err);
#endif
        free(in);
        free(out);
        return -1;
      }
      free(in);
    }
    else
    {
      out = in;
      out_len = e.compressed_size;
    }

    out_crc16 = arc_crc16(out, out_len);
    if(e.crc16 != out_crc16)
    {
#ifdef ARC_DEBUG
      debug("crc16 mismatch: expected %zu, got %zu\n",
       (size_t)e.crc16, (size_t)out_crc16);
#endif
      free(out);
      return -1;
    }

    *dest = out;
    *dest_len = out_len;
    return 0;
  }
}


#ifdef _WIN32
#include <fcntl.h>
#endif

#ifdef LIBFUZZER_FRONTEND
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  FILE *fp = fmemopen((void *)data, size, "rb");
  if(fp)
  {
    unsigned char *out;
    size_t out_len;

    if(arc_read(&out, &out_len, fp, size) == 0)
      free(out);
    fclose(fp);
  }
  return 0;
}

#define main _main
static __attribute__((unused))
#endif

int main(int argc, char *argv[])
{
  FILE *f;
  unsigned char *data;
  size_t data_length;
  unsigned long file_length;

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

  if(arc_read(&data, &data_length, f, file_length) < 0)
    return -1;

  fwrite(data, data_length, 1, stdout);
  free(data);
  return 0;
}
