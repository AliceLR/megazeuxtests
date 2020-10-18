/**
 * Copyright (C) 2020 Lachesis <petrifiedrowan@gmail.com>
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.hpp"

static const char USAGE[] =
  "Check for the ModPlug skip byte extension for .XM files.\n"
  "That's all this currently does.\n\n"
  "Usage:\n"
  "  xmutil [xm files...]\n\n"
  "A list of filenames can be provided through stdin:\n"
  "  ls -1 *.xm | xmutil -\n\n";

static int num_xms;
static int num_invalid_orders;
static int num_without_skip;
static int num_with_skip;
static int num_with_pat_fe;

enum XM_error
{
  XM_SUCCESS,
  XM_READ_ERROR,
  XM_INVALID_MAGIC,
  XM_INVALID_ORDER_COUNT,
  XM_INVALID_PATTERN_COUNT,
};

static const char *XM_strerror(int err)
{
  switch(err)
  {
    case XM_SUCCESS: return "no error";
    case XM_READ_ERROR: return "read error";
    case XM_INVALID_MAGIC: return "file is not an XM";
    case XM_INVALID_ORDER_COUNT: return "invalid order count >256";
    case XM_INVALID_PATTERN_COUNT: return "invalid pattern count >256";
  }
  return "unknown error";
}

struct XM_header
{
  /* 00 */ char magic[17];       // 'Extended Module: '
  /* 17 */ char name[20];        // Null-padded, not null-terminated.
  /* 37 */ uint8_t magic2;       // 0x1a
  /* 38 */ char tracker[20];     // Tracker name.
  /* 58 */ uint16_t version;     // Format version; hi-byte: major, lo-byte: minor.
  /* 60 */ uint32_t header_size;
  /* 64 */ uint16_t num_orders;
  /* 66 */ uint16_t restart_pos;
  /* 68 */ uint16_t num_channels;
  /* 70 */ uint16_t num_patterns;
  /* 72 */ uint16_t num_instruments;
  /* 74 */ uint16_t flags;
  /* 76 */ uint16_t default_tempo;
  /* 78 */ uint16_t default_bpm;
  /* 80 */ uint8_t orders[256];
};

static int read_xm(FILE *fp)
{
  XM_header h;
  bool invalid = false;
  bool mpt_extension = false;
  bool has_fe = false;

  if(!fread(&h, sizeof(XM_header), 1, fp))
    return XM_READ_ERROR;

  if(memcmp(h.magic, "Extended Module: ", 17))
    return XM_INVALID_MAGIC;

  if(h.magic2 != 0x1a)
    return XM_INVALID_MAGIC;

  fix_u16le(h.version);
  fix_u32le(h.header_size);
  fix_u16le(h.num_orders);
  fix_u16le(h.restart_pos);
  fix_u16le(h.num_channels);
  fix_u16le(h.num_patterns);
  fix_u16le(h.num_instruments);
  fix_u16le(h.flags);
  fix_u16le(h.default_tempo);
  fix_u16le(h.default_bpm);

  if(h.num_orders > 256)
    return XM_INVALID_ORDER_COUNT;

  if(h.num_patterns > 256)
    return XM_INVALID_PATTERN_COUNT;

  for(size_t i = 0; i < h.num_orders; i++)
  {
    if(h.orders[i] >= h.num_patterns)
    {
      if(h.orders[i] == 0xFE && !invalid)
        mpt_extension = true;
      else
        invalid = true;
    }
    if(h.orders[i] == 0xFE && h.num_patterns > 0xFE)
      has_fe = true;
  }

  if(invalid)
    num_invalid_orders++;
  else
  if(mpt_extension)
    num_with_skip++;
  else
    num_without_skip++;

  if(has_fe)
    num_with_pat_fe++;
  num_xms++;

  O_("Version : %04x\n", h.version);
  O_("Orders  : %u\n", h.num_orders);
  O_("Patterns: %u%s\n", h.num_patterns, has_fe ? " (uses 0xFE)" : "");
  O_("Invalid?: %s\n\n",
    invalid ? mpt_extension ? "YES (incl. 0xFE)" : "YES" :
    mpt_extension ? "ModPlug skip" :
    "NO"
  );

  return XM_SUCCESS;
}

static void check_xm(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("File    : %s\n", filename);

    int err = read_xm(fp);
    if(err)
      O_("Error: %s\n\n", XM_strerror(err));

    fclose(fp);
  }
  else
    O_("Failed to open '%s'.\n\n", filename);
}


int main(int argc, char *argv[])
{
  if(!argv || argc < 2)
  {
    fprintf(stderr, "%s", USAGE);
    return 0;
  }

  bool read_stdin = false;
  for(int i = 1; i < argc; i++)
  {
    if(!strcmp(argv[i], "-"))
    {
      if(!read_stdin)
      {
        char buffer[1024];
        while(fgets_safe(buffer, stdin))
          check_xm(buffer);
        read_stdin = true;
      }
      continue;
    }
    check_xm(argv[i]);
  }

  if(num_xms)
    O_("Total XMs        : %d\n", num_xms);
  if(num_without_skip)
    O_("XMs without skip : %d\n", num_without_skip);
  if(num_with_skip)
    O_("XMs with skip    : %d\n", num_with_skip);
  if(num_invalid_orders)
    O_("XMs with inval.  : %d\n", num_invalid_orders);
  if(num_with_pat_fe)
    O_("XMs with pat. FE : %d\n", num_with_pat_fe);
  return 0;
}
