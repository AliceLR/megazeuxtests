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

/**
 * Based on the DSIK module format documentation here:
 *
 * http://www.shikadi.net/moddingwiki/DSIK_Module_Format
 */

#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Config.hpp"
#include "IFF.hpp"
#include "common.hpp"
#include "format.hpp"
#include "modutil.hpp"

static int total_dsik = 0;


enum DSIK_features
{
  FT_ORDERS_OVER_128,
  FT_ROWS_OVER_64,
  FT_ROWS_OVER_128,
  FT_CHUNK_OVER_4_MIB,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  ">128Orders",
  ">64Rows",
  ">128Rows",
  ">4MBChunk",
};

static const int MAX_SAMPLES  = 256;
static const int MAX_PATTERNS = 256;
static const int MAX_ORDERS   = 128;
static const int MAX_CHANNELS = 16;

enum DSIK_type
{
  DSM_1_0,
  DSMF_RIFF,
  DSMF_VARIANT
};

struct DSIK_song
{
  /*  0 */ char     name[28];
  /* 28 */ uint16_t format_version;
  /* 30 */ uint16_t flags;
  /* 32 */ uint32_t unused;
  /* 36 */ uint16_t num_orders;
  /* 38 */ uint16_t num_samples;
  /* 40 */ uint16_t num_patterns;
  /* 42 */ uint16_t num_channels;
  /* 44 */ uint8_t  global_volume;
  /* 45 */ uint8_t  master_volume;
  /* 46 */ uint8_t  initial_speed;
  /* 47 */ uint8_t  initial_tempo;
  /* 48 */ uint8_t  channel_map[16];
  /* 64 */ uint8_t  orders[128];
};

struct DSIK_sample
{
  enum flags
  {
    LOOP   = (1<<0),
    SIGNED = (1<<1),
    PACKED = (1<<2),
    DELTA  = (1<<6),
  };

  /*  0 */ char     filename[13];
  /* 13 */ uint16_t flags;
  /* 15 */ uint8_t  default_volume;
  /* 16 */ uint32_t length;     /* in bytes? */
  /* 20 */ uint32_t loop_start; /* " */
  /* 24 */ uint32_t loop_end;   /* " */
  /* 28 */ uint32_t ignore;
  /* 32 */ uint16_t c4rate;
  /* 34 */ uint16_t period; /* ? */
  /* 36 */ char     name[28];
  /* 64    sample data.... */
};

struct DSIK_pattern
{
  enum flags
  {
    NOTE       = (1<<7),
    INSTRUMENT = (1<<6),
    VOLUME     = (1<<5),
    EFFECT     = (1<<4),
    CHANNEL    = 0xF
  };

  struct event
  {
    uint8_t flags;
    uint8_t note;
    uint8_t instrument;
    uint8_t volume;
    uint8_t effect;
    uint8_t param;
  };

  uint16_t length_in_bytes;
  uint16_t num_rows; /* should always be 64 ? */

  event *data;

  ~DSIK_pattern()
  {
    delete[] data;
  }
};

struct DSIK_data
{
  /* Header (12 or, in rare cases, 16)
   *
   * Standard header (12): "RIFF", riff size, "DSMF".
   *
   * Variant header (16): prefixed with an extra DSMF,
   * usually has "RIFF" blanked out at position 4,
   * the RIFF length at position 8, and finally "DSMF"
   * (also sometimes blanked out) at position 12.
   *
   * v1.0 header (4): DSM\x10. Not supported...
   */

  char header[16];
  enum DSIK_type type;

  /* SONG (192) */

  DSIK_song song;

  /* INST (64 + data) */

  int current_sample = 0;
  DSIK_sample samples[MAX_SAMPLES];

  /* PATT (2 + data length) */

  int current_pattern = 0;
  DSIK_pattern patterns[MAX_PATTERNS];

  bool uses[NUM_FEATURES];
};

static const class DSIK_SONG_Handler final: public IFFHandler<DSIK_data>
{
public:
  DSIK_SONG_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DSIK_data &m) const override
  {
    if(len < 192)
    {
      format::error("SONG chunk length < 192.");
      return modutil::INVALID;
    }

    DSIK_song &s = m.song;

    if(!fread(s.name, 28, 1, fp))
      return modutil::READ_ERROR;
    s.name[27] = '\0';

    s.format_version = fget_u16le(fp);
    s.flags          = fget_u16le(fp);
    s.unused         = fget_u32le(fp);
    s.num_orders     = fget_u16le(fp);
    s.num_samples    = fget_u16le(fp);
    s.num_patterns   = fget_u16le(fp);
    s.num_channels   = fget_u16le(fp);
    s.global_volume  = fgetc(fp);
    s.master_volume  = fgetc(fp);
    s.initial_speed  = fgetc(fp);
    s.initial_tempo  = fgetc(fp);

    if(s.num_orders > MAX_ORDERS)
    {
      format::error("order count %u > %u.", s.num_orders, MAX_ORDERS);
      return modutil::INVALID;
    }
    if(s.num_samples > MAX_SAMPLES)
    {
      format::error("sample count %u > %u.", s.num_samples, MAX_SAMPLES);
      return modutil::INVALID;
    }
    if(s.num_patterns > MAX_PATTERNS)
    {
      format::error("pattern count %u > %u.", s.num_patterns, MAX_PATTERNS);
      return modutil::INVALID;
    }
    if(s.num_channels > MAX_CHANNELS)
    {
      format::error("channel count %u > %u.", s.num_channels, MAX_CHANNELS);
      return modutil::INVALID;
    }

    if(!fread(s.channel_map, 16, 1, fp))
      return modutil::READ_ERROR;

    if(!fread(s.orders, 128, 1, fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
} SONG_handler("SONG", false);

static const class DSIK_INST_Handler final: public IFFHandler<DSIK_data>
{
public:
  DSIK_INST_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DSIK_data &m) const override
  {
    if(len < 64)
    {
      format::error("INST chunk length < 64.");
      return modutil::INVALID;
    }
    if(m.current_sample >= MAX_SAMPLES)
    {
      format::warning("ignoring sample %u.", m.current_sample);
      return modutil::SUCCESS;
    }

    DSIK_sample &sm = m.samples[m.current_sample++];

    if(!fread(sm.filename, 13, 1, fp))
      return modutil::READ_ERROR;
    sm.filename[12] = '\0';

    sm.flags          = fget_u16le(fp);
    sm.default_volume = fgetc(fp);
    sm.length         = fget_u32le(fp);
    sm.loop_start     = fget_u32le(fp);
    sm.loop_end       = fget_u32le(fp);
    sm.ignore         = fget_u32le(fp);
    sm.c4rate         = fget_u16le(fp);
    sm.period         = fget_u16le(fp);

    if(!fread(sm.name, 28, 1, fp))
      return modutil::READ_ERROR;
    sm.name[27] = '\0';

    /* ignore sample data. */

    return modutil::SUCCESS;
  }
} INST_handler("INST", false);

static const class DSIK_PATT_Handler final: public IFFHandler<DSIK_data>
{
public:
  DSIK_PATT_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DSIK_data &m) const override
  {
    DSIK_song &s = m.song;
    if(len < 2)
    {
      format::error("PATT chunk length < 2.");
      return modutil::INVALID;
    }
    if(m.current_pattern >= MAX_PATTERNS)
    {
      format::warning("ignoring pattern %u.", m.current_pattern);
      return modutil::SUCCESS;
    }

    DSIK_pattern &p = m.patterns[m.current_pattern++];

    p.length_in_bytes = fget_u16le(fp);
    if(p.length_in_bytes < 2)
    {
      format::error("pattern %u length field invalid %d.",
        m.current_pattern - 1, static_cast<int>(p.length_in_bytes) - 2);
      return modutil::INVALID;
    }
    p.length_in_bytes -= 2;

    std::unique_ptr<uint8_t[]> buffer(new uint8_t[p.length_in_bytes]);
    if(!fread(buffer.get(), p.length_in_bytes, 1, fp))
      return modutil::READ_ERROR;

    // Prescan row count.
    size_t i;
    p.num_rows = 0;
    for(i = 0; i < p.length_in_bytes;)
    {
      uint8_t f = buffer[i++];
      if(f)
      {
        size_t ch = f & DSIK_pattern::CHANNEL;
        if(ch > s.num_channels)
        {
          format::error("invalid channel %zu referenced in pattern %u.", ch, m.current_pattern - 1);
          return modutil::INVALID;
        }

        if(f & DSIK_pattern::NOTE)
          i++;

        if(f & DSIK_pattern::INSTRUMENT)
          i++;

        if(f & DSIK_pattern::VOLUME)
          i++;

        if(f & DSIK_pattern::EFFECT)
          i += 2;
      }
      else
        p.num_rows++;
    }

    if(i > p.length_in_bytes)
    {
      format::error("invalid pattern data in pattern %u.", m.current_pattern - 1);
      return modutil::INVALID;
    }

    if(p.num_rows > 128)
      m.uses[FT_ROWS_OVER_128] = true;
    else
    if(p.num_rows > 64)
      m.uses[FT_ROWS_OVER_64] = true;

    p.data = new DSIK_pattern::event[p.num_rows * s.num_channels]{};
    DSIK_pattern::event *row = p.data;

    for(i = 0; i < p.length_in_bytes;)
    {
      uint8_t f = buffer[i++];
      if(f)
      {
        size_t ch = f & DSIK_pattern::CHANNEL;

        DSIK_pattern::event *ev = row + ch;
        ev->flags = f;

        if(f & DSIK_pattern::NOTE)
          ev->note = buffer[i++];
        if(f & DSIK_pattern::INSTRUMENT)
          ev->instrument = buffer[i++];
        if(f & DSIK_pattern::VOLUME)
          ev->volume = buffer[i++];
        if(f & DSIK_pattern::EFFECT)
        {
          ev->effect = buffer[i++];
          ev->param  = buffer[i++];
        }
      }
      else
        row += s.num_channels;
    }
    return modutil::SUCCESS;
  }
} PATT_handler("PATT", false);

static const IFF<DSIK_data> DSIK_parser(Endian::LITTLE, IFFPadding::BYTE,
{
  &SONG_handler,
  &INST_handler,
  &PATT_handler,
});


modutil::error DSIK_read(FILE *fp)
{
  DSIK_data m{};
  DSIK_song &s = m.song;
  DSIK_parser.max_chunk_length = 0;

  if(!fread(m.header, 12, 1, fp))
    return modutil::READ_ERROR;

  if(!strncmp(m.header + 0, "RIFF", 4) && !strncmp(m.header + 8, "DSMF", 4))
  {
    m.type = DSMF_RIFF;
  }
  else

  if(!strncmp(m.header + 0, "DSMF", 4))
  {
    m.type = DSMF_VARIANT;

    if(!fread(m.header + 12, 4, 1, fp))
      return modutil::READ_ERROR;
  }
  else

  if(!strncmp(m.header + 0, "DSM\x10", 4))
  {
    total_dsik++;
    return modutil::DSIK_OLD_FORMAT;
  }
  else
    return modutil::FORMAT_ERROR;

  total_dsik++;

  modutil::error err = DSIK_parser.parse_iff(fp, 0, m);
  if(err)
    return err;

  if(DSIK_parser.max_chunk_length > 4*1024*1024)
    m.uses[FT_CHUNK_OVER_4_MIB] = true;

  format::line("Name",     "%s", s.name);
  format::line("Type",     "DSM %04u", s.format_version);
  format::line("Samples",  "%u", s.num_samples);
  format::line("Channels", "%u", s.num_channels);
  format::line("Patterns", "%u", s.num_patterns);
  format::line("Orders",   "%u", s.num_orders);
  format::line("MaxChunk", "%zu", DSIK_parser.max_chunk_length);
  format::uses(m.uses, FEATURE_STR);

  if(Config.dump_samples)
  {
    format::line();

    static const char PAD[] = "------------------------------";
    O_("Samples : %-27.27s  %-12.12s : %-10.10s %-10.10s %-10.10s : Vol  C4Rate  Period  Flags :\n",
      "Name", "Filename", "Length", "LoopStart", "LoopEnd");
    O_("------- : %-27.27s  %-12.12s : %-10.10s %-10.10s %-10.10s : ---  ------  ------  ----- :\n",
      PAD, PAD, PAD, PAD, PAD);

    for(unsigned int i = 0; i < s.num_samples; i++)
    {
      DSIK_sample &sm = m.samples[i];
      O_("    %02x  : %-27.27s  %-12.12s : %-10u %-10u %-10u : %-3u  %-6u  %-6u  %-5u :\n",
        i + 1, sm.name, sm.filename,
        sm.length, sm.loop_start, sm.loop_end,
        sm.default_volume, sm.c4rate, sm.period, sm.flags
      );
    }
  }

  if(Config.dump_patterns)
  {
    format::line();
    format::orders("Orders", s.orders, s.num_orders);

    if(!Config.dump_pattern_rows)
      format::line();

    for(unsigned int i = 0; i < s.num_patterns; i++)
    {
      if(i >= MAX_PATTERNS)
        break;

      DSIK_pattern &p = m.patterns[i];

      using EVENT = format::event<format::value, format::value, format::value, format::effectWide>;
      format::pattern<EVENT> pattern(i, s.num_channels, p.num_rows, p.length_in_bytes);

      if(!Config.dump_pattern_rows)
      {
        pattern.summary();
        continue;
      }

      DSIK_pattern::event *current = p.data;

      for(unsigned int row = 0; row < p.num_rows; row++)
      {
        for(unsigned int track = 0; track < s.num_channels; track++, current++)
        {
          format::value      a{ current->note };
          format::value      b{ current->instrument };
          format::value      c{ current->volume };
          format::effectWide d{ current->effect, current->param };

          pattern.insert(EVENT(a, b, c, d));
        }
      }
      pattern.print();
    }
  }
  return modutil::SUCCESS;
}

class DSIK_loader : modutil::loader
{
public:
  DSIK_loader(): modutil::loader("DSM : Digital Sound Interface Kit") {}

  virtual modutil::error load(FILE *fp) const override
  {
    return DSIK_read(fp);
  }

  virtual void report() const override
  {
    if(!total_dsik)
      return;

    fprintf(stderr, "\n");
    O_("Total DSM           : %d\n", total_dsik);
    O_("------------------- :\n");
  }
};

static const DSIK_loader loader;
