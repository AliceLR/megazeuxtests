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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "lzx_unpack.h"

/**
 * Header base was reverse engineered with a hex editor and then filled in
 * with details from the documentation comments in unlzx.c (unknown license).
 * All usage of unlzx.c is directly stated and is probably non-copyrightable.
 */

/* #define LZX_DEBUG */

/* Arbitrary output maximum file length. */
#define LZX_OUTPUT_MAX (1 << 29)

#define LZX_HEADER_SIZE 10
#define LZX_ENTRY_SIZE  31
#define LZX_FLAG_MERGED 1

#ifdef LZX_DEBUG
#define debug(...) do{ fprintf(stderr, "" __VA_ARGS__); fflush(stderr); }while(0)
#endif

static lzx_uint32 lzx_crc32(lzx_uint32 crc, unsigned char *buf, size_t len)
{
  return dimgutil_crc32(crc, buf, len);
}

static inline lzx_uint32 lzx_mem_u32(const unsigned char *buf)
{
  return (buf[3] << 24UL) | (buf[2] << 16UL) | (buf[1] << 8UL) | buf[0];
}

enum lzx_merge_state
{
  NO_MERGE,
  IN_MERGE,
  FINAL_MERGE_ENTRY,
};

struct lzx_data
{
  /*  0 */ char      magic[3];         /* "LZX" */
  /*  3    lzx_uint8 unknown0; */      /* Claimed to be flags by unlzx.c */
  /*  4    lzx_uint8 lzx_version; */   /* 0x0 for <=1.20R, 0xc for >=1.21 */
  /*  5    lzx_uint8 unknown1; */
  /*  6    lzx_uint8 format_version;*/ /* 0xa */
  /*  7    lzx_uint8 flags; */
  /*  8    lzx_uint8 unknown2[2]; */
  /* 10 */

  /* Most of the above info is guessed due to lack of documentation.
   *
   * The non-zero header bytes seem to be tied to the version used.
   * Byte 6 is always 0x0a, and is maybe intended to be the format version.
   * Byte 4 is always 0x0c for versions >=1.21 and may be intended to be the
   * LZX archiver version (0xc -> 1.2, similar to 0xa -> 1.0 for the format).
   * Byte 7 is used for flags. 1=damage protection, 2=locked. 4=unknown
   * is always set for versions >=1.21. None of these flags are documented.
   */

  /* Data for the current merge record to allow reading in one pass.
   * A merged record starts with an entry with a 0 compressed size and the
   * merged flag set, and ends when a compressed size is encountered. */
  enum lzx_merge_state merge_state;
  int merge_invalid;
  size_t merge_total_size;
  size_t selected_offset;
  size_t selected_size;
  lzx_uint32 selected_crc32;
};

struct lzx_entry
{
  /*  0    lzx_uint8  attributes; */
  /*  1    lzx_uint8  unknown0; */
  /*  2 */ lzx_uint32 uncompressed_size;
  /*  6 */ lzx_uint32 compressed_size;
  /* 10    lzx_uint8  machine_type; */ /* unlzx.c */
  /* 11 */ lzx_uint8  method;          /* unlzx.c */
  /* 12 */ lzx_uint8  flags;           /* unlzx.c */
  /* 13    lzx_uint8  unknown1; */
  /* 14 */ lzx_uint8  comment_length;  /* unlzx.c; = m */
  /* 15 */ lzx_uint8  extract_version; /* unlzx.c; should be 0x0A? */
  /* 16    lzx_uint16 unknown2; */
  /* 18    lzx_uint32 datestamp; */    /* unlzx.c */
  /* 22 */ lzx_uint32 crc32;           /* unlzx.c */
  /* 26 */ lzx_uint32 header_crc32;    /* unlzx.c */
  /* 30 */ lzx_uint8  filename_length; /* = n */
  /* 31 */ lzx_uint8  filename[256];
  /* 31 + n + m */

  lzx_uint32 computed_header_crc32;

/* Date packing (quoted directly from unlzx.c):
 *
 *  "UBYTE packed[4]; bit 0 is MSB, 31 is LSB
 *   bit # 0-4=Day 5-8=Month 9-14=Year 15-19=Hour 20-25=Minute 26-31=Second"
 *
 * Year interpretation is non-intuitive due to bugs in the original LZX, but
 * Classic Workbench bundles Dr.Titus' fixed LZX, which interprets years as:
 *
 *   001000b to 011101b -> 1978 to 1999  Original range
 *   111010b to 111111b -> 2000 to 2005  Original-compatible Y2K bug range
 *   011110b to 111001b -> 2006 to 2033  Dr.Titus extension
 *   000000b to 000111b -> 2034 to 2041  Dr.Titus extension (reserved values)
 *
 * The buggy original range is probably caused by ([2 digit year] - 70) & 63.
 */
};

static int lzx_read_header(struct lzx_data *lzx, FILE *f)
{
  unsigned char buf[LZX_HEADER_SIZE];

  if(fread(buf, 1, LZX_HEADER_SIZE, f) < LZX_HEADER_SIZE)
    return -1;

  if(memcmp(buf, "LZX", 3))
    return -1;

  memset(lzx, 0, sizeof(struct lzx_data));
  lzx->selected_offset = SIZE_MAX;
  return 0;
}

static int lzx_read_entry(struct lzx_entry *e, FILE *f)
{
  unsigned char buf[256];
  lzx_uint32 crc;

  /* unlzx.c claims there's a method 32 for EOF, but nothing like this
   * has shown up. Most LZX archives just end after the last file. */

  if(fread(buf, 1, LZX_ENTRY_SIZE, f) < LZX_ENTRY_SIZE)
    return -1;

  e->uncompressed_size = lzx_mem_u32(buf + 2);
  e->compressed_size   = lzx_mem_u32(buf + 6);
  e->method            = buf[11];
  e->flags             = buf[12];
  e->comment_length    = buf[14];
  e->extract_version   = buf[15];
  e->crc32             = lzx_mem_u32(buf + 22);
  e->header_crc32      = lzx_mem_u32(buf + 26);
  e->filename_length   = buf[30];

  /* The header CRC is taken with its field 0-initialized. (unlzx.c) */
  memset(buf + 26, 0, 4);

  crc = lzx_crc32(0, buf, LZX_ENTRY_SIZE);

  if(e->filename_length)
  {
    if(fread(e->filename, 1, e->filename_length, f) < e->filename_length)
      return -1;

    crc = lzx_crc32(crc, e->filename, e->filename_length);
  }
  e->filename[e->filename_length] = '\0';

  /* Mostly assuming this part because the example files don't have it. */
  if(e->comment_length)
  {
    if(fread(buf, 1, e->comment_length, f) < e->comment_length)
      return -1;

    crc = lzx_crc32(crc, buf, e->comment_length);
  }
  e->computed_header_crc32 = crc;
  return 0;
}

static void lzx_reset_merge(struct lzx_data *lzx)
{
  lzx->merge_state = NO_MERGE;
  lzx->merge_invalid = 0;
  lzx->merge_total_size = 0;
  lzx->selected_offset = SIZE_MAX;
}

static int lzx_has_selected_file(struct lzx_data *lzx)
{
  return lzx->selected_offset < SIZE_MAX;
}

static void lzx_select_file(struct lzx_data *lzx, const struct lzx_entry *e)
{
  if(!lzx_has_selected_file(lzx))
  {
    /* For multiple file output, use a queue here instead... */
    lzx->selected_offset = lzx->merge_total_size;
    lzx->selected_size = e->uncompressed_size;
    lzx->selected_crc32 = e->crc32;
    #ifdef LZX_DEBUG
    debug("selecting file '%s'\n", e->filename);
    debug("  offset: %zu size: %zu crc: %08zx\n",
     lzx->selected_offset, lzx->selected_size, (size_t)lzx->selected_crc32);
    #endif
  }
}

static int lzx_check_entry(struct lzx_data *lzx, const struct lzx_entry *e, size_t file_len)
{
  int selectable = 1;

  #ifdef LZX_DEBUG
  debug("checking file '%s'\n", e->filename);
  #endif

  /* Filter unsupported or junk files. */
  if(e->header_crc32 != e->computed_header_crc32 ||
     e->compressed_size >= file_len ||
     e->uncompressed_size > LZX_OUTPUT_MAX ||
     e->extract_version > 0x0a ||
     lzx_method_is_supported(e->method) < 0)
  {
    #ifdef LZX_DEBUG
    if(e->header_crc32 != e->computed_header_crc32)
    {
      debug("skipping file: header CRC-32 mismatch (got 0x%08zx, expected 0x%08zx)\n",
       (size_t)e->computed_header_crc32, (size_t)e->header_crc32);
    }
    else
    {
      debug("skipping file: unsupported file (u:%zu c:%zu ver:%u method:%u flag:%u)\n",
       (size_t)e->uncompressed_size, (size_t)e->compressed_size, e->extract_version,
       e->method, e->flags);
    }
    #endif
    lzx->merge_invalid = 1;
    selectable = 0;
  }
  if(e->uncompressed_size == 0)
    selectable = 0;

  if(e->flags & LZX_FLAG_MERGED)
  {
    if(lzx->merge_state != IN_MERGE)
    {
      lzx_reset_merge(lzx);
      lzx->merge_state = IN_MERGE;
    }

    /* Check overflow for 32-bit systems and other unsupported things. */
    if(lzx->merge_invalid ||
       e->method != LZX_M_PACKED ||
       lzx->merge_total_size + e->uncompressed_size < lzx->merge_total_size ||
       lzx->merge_total_size + e->uncompressed_size > LZX_OUTPUT_MAX)
    {
      lzx->merge_invalid = 1;
      selectable = 0;
    }

    if(selectable)
      lzx_select_file(lzx, e);

    lzx->merge_total_size += e->uncompressed_size;
    if(e->compressed_size)
    {
      lzx->merge_state = FINAL_MERGE_ENTRY;
      if(lzx_has_selected_file(lzx) && !lzx->merge_invalid)
        return 0;
    }
    /* Continue until a usable entry with compressed data is found. */
    return -1;
  }

  /* Not merged */
  lzx_reset_merge(lzx);
  if(selectable)
  {
    lzx_select_file(lzx, e);
    lzx->merge_total_size += e->uncompressed_size;
    return 0;
  }
  return -1;
}

static int lzx_read(unsigned char **dest, size_t *dest_len, FILE *f, unsigned long file_len)
{
  struct lzx_data lzx;
  struct lzx_entry e;
  unsigned char *out;
  unsigned char *in;
  size_t out_len;
  lzx_uint32 out_crc32;
  int err;

  if(lzx_read_header(&lzx, f) < 0)
    return -1;

  while(1)
  {
    if(lzx_read_entry(&e, f) < 0)
    {
      #ifdef LZX_DEBUG
      debug("failed to read entry\n");
      #endif
      return -1;
    }

    if(lzx_check_entry(&lzx, &e, file_len) < 0)
    {
      if(e.compressed_size && fseek(f, e.compressed_size, SEEK_CUR) < 0)
        return -1;
      continue;
    }

    #ifdef LZX_DEBUG
    debug("extracting file '%s'\n", e.filename);
    #endif

    /* Extract */
    in = (unsigned char *)malloc(e.compressed_size);
    if(in == NULL)
      return -1;

    if(fread(in, 1, e.compressed_size, f) < e.compressed_size)
    {
      free(in);
      return -1;
    }

    if(e.method != LZX_M_UNPACKED)
    {
      out = (unsigned char *)malloc(lzx.merge_total_size);
      out_len = lzx.merge_total_size;
      if(out == NULL)
      {
        free(in);
        return -1;
      }

      err = lzx_unpack(out, out_len, in, e.compressed_size, e.method);
      free(in);

      if(err)
      {
        #ifdef LZX_DEBUG
        debug("unpack failed\n");
        #endif
        free(out);
        return -1;
      }
    }
    else
    {
      out = in;
      out_len = e.compressed_size;
    }

    /* Select a file from a merge (if needed). */
    if(lzx.selected_size < out_len)
    {
      unsigned char *t;
      #ifdef LZX_DEBUG
      debug("using data pos:%zu len:%zu in merge of length %zu\n",
       lzx.selected_offset, lzx.selected_size, lzx.merge_total_size);
      #endif
      if(lzx.selected_offset && lzx.selected_offset <= out_len - lzx.selected_size)
        memmove(out, out + lzx.selected_offset, lzx.selected_size);

      out_len = lzx.selected_size;
      t = (unsigned char *)realloc(out, out_len);
      if(t != NULL)
        out = t;
    }

    out_crc32 = lzx_crc32(0, out, out_len);
    if(out_crc32 != lzx.selected_crc32)
    {
      #ifdef LZX_DEBUG
      debug("file CRC-32 mismatch (got 0x%08zx, expected 0x%08zx)\n",
       (size_t)out_crc32, (size_t)lzx.selected_crc32);
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

    if(lzx_read(&out, &out_len, fp, size) == 0)
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

  if(lzx_read(&data, &data_length, f, file_length) < 0)
    return -1;

  #ifdef LZX_DEBUG
  debug("file decompressed successfully.\n");
  #endif

  //fwrite(data, data_length, 1, stdout);
  free(data);
  return 0;
}
