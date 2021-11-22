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
 * Protracker Studio 16 / Epic MegaGames MASI "old format" handler.
 * See psm_load.cpp for the newer format.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

static int total_ps16 = 0;


enum PS16_features
{
  FT_ROWS_OVER_64,
  FT_SAMPLE_OVER_64K,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "P:>64Rows",
  "S:>64k",
};

static const int MAX_SAMPLES  = 256;
static const int MAX_PATTERNS = 256;
static const int MAX_ORDERS   = 256;
static const int MAX_CHANNELS = 32;

static constexpr char MAGIC[] = "PSM\xfe";

struct PS16_pattern
{
  int lol;
};

struct PS16_header
{
  /*   0 */ uint8_t  magic[4];
  /*   4 */ char     name[59];
  /*  63 */ uint8_t  eof;
  /*  64 */ uint8_t  type;
  /*  65 */ uint8_t  version; /* high nibble: major; low nibble: minor*/
  /*  66 */ uint8_t  pattern_version; /* 1 seems to mean it uses a "255 channel format" and isn't used?? */
  /*  67 */ uint8_t  init_speed;
  /*  68 */ uint8_t  init_bpm;
  /*  69 */ uint8_t  global_volume;
  /*  70 */ uint16_t num_orders;
  /*  72 */ uint16_t num_orders2; /* The same as num_orders; this format was originally meant to store multiple sequences. */
  /*  74 */ uint16_t num_patterns;
  /*  76 */ uint16_t num_samples;
  /*  78 */ uint16_t num_channels_play; /* Number of channels to play. */
  /*  80 */ uint16_t num_channels; /* Number of channels to process. */
  /*  82 */ uint32_t orders_offset;
  /*  86 */ uint32_t panning_offset;
  /*  90 */ uint32_t patterns_offset;
  /*  94 */ uint32_t samples_offset;
  /*  98 */ uint32_t comments_offset;
  /* 102 */ uint32_t total_pattern_size;
  /* 106 */ uint8_t  reserved[40];
  /* 146 */
};

struct PS16_data
{
  PS16_header header;

  bool uses[NUM_FEATURES];
};

class PS16_loader : modutil::loader
{
public:
  PS16_loader(): modutil::loader("PSM", "ps16", "Protracker Studio 16 / Epic MegaGames MASI") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    PS16_data m{};
    PS16_header &h = m.header;
    uint8_t buf[256];

    if(!fread(h.magic, 4, 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(h.magic, MAGIC, 4))
      return modutil::FORMAT_ERROR;

    total_ps16++;

    /* Header */
    if(!fread(buf + 4, 142, 1, fp))
      return modutil::READ_ERROR;

    memcpy(h.name, buf + 4, sizeof(h.name));
    h.name[sizeof(h.name) - 1] = '\0';
    strip_module_name(h.name, sizeof(h.name));

    h.eof                = buf[63];
    h.type               = buf[64];
    h.version            = buf[65];
    h.pattern_version    = buf[66];
    h.init_speed         = buf[67];
    h.init_bpm           = buf[68];
    h.global_volume      = buf[69];
    h.num_orders         = mem_u16le(buf + 70);
    h.num_orders2        = mem_u16le(buf + 72);
    h.num_patterns       = mem_u16le(buf + 74);
    h.num_samples        = mem_u16le(buf + 76);
    h.num_channels_play  = mem_u16le(buf + 78);
    h.num_channels       = mem_u16le(buf + 80);
    h.orders_offset      = mem_u32le(buf + 82);
    h.panning_offset     = mem_u32le(buf + 86);
    h.patterns_offset    = mem_u32le(buf + 90);
    h.samples_offset     = mem_u32le(buf + 94);
    h.comments_offset    = mem_u32le(buf + 98);
    h.total_pattern_size = mem_u32le(buf + 102);
    //memcpy(reserved, buf + 106, 40);

    /* Orders */
    /* Panning */
    /* Patterns */
    /* Samples */
    /* Comment */


    /* Print information */

    format::line("Name", "%s", h.name);
    format::line("Type", "MASI PS16 v%d.%02d", h.version >> 4, h.version & 0xf);

    format::line("Samples",  "%u", h.num_samples);
    format::line("Channels", "%u", h.num_channels);
    format::line("Patterns", "%u", h.num_patterns);
    format::line("Orders",   "%u", h.num_orders);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      // FIXME
    }

    if(Config.dump_patterns)
    {
      format::line();
/*
      for(unsigned int i = 0; i < m.num_patterns; i++)
      {
        if(i >= MAX_PATTERNS)
          break;

        PSM_pattern &p = m.patterns[i];

        // FIXME
        O_("Pat. %02x : '%s', %u rows\n", i, p.id, p.num_rows);
      }
*/
      // FIXME Config.dump_pattern_rows
    }
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!total_ps16)
      return;

    format::report("Total PS16s", total_ps16);
  }
};

static const PS16_loader loader;
