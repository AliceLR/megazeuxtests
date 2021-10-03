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

#include "modutil.hpp"

static int num_xms;
static int num_invalid_orders;
static int num_with_skip;
static int num_with_pat_fe;


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


class XM_loader : public modutil::loader
{
public:
  XM_loader(): modutil::loader("XM  : Extended Module") {}

  virtual modutil::error load(FILE *fp) const override
  {
    XM_header h;
    bool invalid = false;
    bool mpt_extension = false;
    bool has_fe = false;

    if(!fread(&h, sizeof(XM_header), 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(h.magic, "Extended Module: ", 17))
      return modutil::FORMAT_ERROR;

    if(h.magic2 != 0x1a)
      return modutil::FORMAT_ERROR;

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
      return modutil::XM_INVALID_ORDER_COUNT;

    if(h.num_patterns > 256)
      return modutil::XM_INVALID_PATTERN_COUNT;

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

    if(has_fe)
      num_with_pat_fe++;
    num_xms++;

    format::line("Type",     "XM %04x", h.version);
    format::line("Orders",   "%u", h.num_orders);
    format::line("Patterns", "%u%s", h.num_patterns, has_fe ? " (uses 0xFE)" : "");
    format::line("Invalid?", "%s",
      invalid ? mpt_extension ? "YES (incl. 0xFE)" : "YES" :
      mpt_extension ? "ModPlug skip" :
      "NO"
    );

    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!num_xms)
      return;

    format::report("Total XMs", num_xms);

    if(num_with_skip)
      format::reportline("XMs with skip", "%d", num_with_skip);
    if(num_invalid_orders)
      format::reportline("XMs with inval.", "%d", num_invalid_orders);
    if(num_with_pat_fe)
      format::reportline("XMs with pat. FE", "%d", num_with_pat_fe);
  }
};

static const XM_loader loader;
