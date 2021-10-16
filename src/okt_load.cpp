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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "IFF.hpp"
#include "modutil.hpp"

static int total_okts = 0;


enum OKT_features
{
  FT_ROWS_OVER_64,
  FT_ROWS_OVER_128,
  FT_CHUNK_OVER_4_MIB,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  ">64Rows",
  ">128Rows",
  ">4MBChunk",
};

static const int MAX_SAMPLES  = 256;
static const int MAX_PATTERNS = 256;
static const int MAX_ORDERS   = 256;

struct OKT_sample
{
  char name[21]; /* stored as 20. */
  uint32_t length;
  uint16_t repeat_start;
  uint16_t repeat_length;
  /* u8 pad */
  uint8_t  volume;
  /* u16 pad */
};

struct OKT_pattern
{
  struct event
  {
    uint8_t note;
    uint8_t instrument;
    uint8_t effect;
    uint8_t param;
  };

  uint16_t num_rows;
  event *data;

  ~OKT_pattern()
  {
    delete[] data;
  }
};

struct OKT_data
{
  /* Header (8) */

  char magic[8]; /* OKTASONG */

  /* CMOD (8) */

  uint16_t chan_flags[4];
  unsigned int num_channels = 0;

  /* SAMP (number of samples * 32) */

  int num_samples;
  OKT_sample samples[MAX_SAMPLES];

  /* SPEE (2) */

  uint16_t initial_tempo;

  /* SLEN (2) */

  uint16_t num_patterns;

  /* PLEN (2) */

  uint16_t num_orders;

  /* PATT (128) */

  uint8_t orders[MAX_ORDERS];

  /* PBOD (2 + channel count * line count * 4) */

  uint16_t current_patt = 0;
  OKT_pattern patterns[MAX_PATTERNS];

  bool uses[NUM_FEATURES];
};

static const class OKT_CMOD_Handler final: public IFFHandler<OKT_data>
{
public:
  OKT_CMOD_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, OKT_data &m) const override
  {
    for(int i = 0; i < 4; i++)
    {
      uint16_t v = fget_u16be(fp);
      m.chan_flags[i] = v;

      if(v & 0x01)
        m.num_channels += 2;
      else
        m.num_channels++;
    }

    if(feof(fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
} CMOD_handler("CMOD", false);

static const class OKT_SAMP_Handler final: public IFFHandler<OKT_data>
{
public:
  OKT_SAMP_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, OKT_data &m) const override
  {
    int num_samples = len / 32;
    m.num_samples = num_samples;

    for(int i = 0; i < num_samples; i++)
    {
      OKT_sample &s = m.samples[i];

      if(!fread(s.name, 20, 1, fp))
        return modutil::READ_ERROR;
      s.name[20] = '\0';

      s.length        = fget_u32be(fp);
      s.repeat_start  = fget_u16be(fp);
      s.repeat_length = fget_u16be(fp);
      fgetc(fp);
      s.volume        = fgetc(fp);
      fget_u16be(fp);
    }
    if(feof(fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
} SAMP_handler("SAMP", false);

static const class OKT_SPEE_Handler final: public IFFHandler<OKT_data>
{
public:
  OKT_SPEE_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, OKT_data &m) const override
  {
    m.initial_tempo = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;
    return modutil::SUCCESS;
  }
} SPEE_handler("SPEE", false);

static const class OKT_SLEN_Handler final: public IFFHandler<OKT_data>
{
public:
  OKT_SLEN_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, OKT_data &m) const override
  {
    m.num_patterns = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    if(m.num_patterns > MAX_PATTERNS)
    {
      format::error("too many patterns in SLEN (%u)", m.num_patterns);
      return modutil::INVALID;
    }
    return modutil::SUCCESS;
  }
} SLEN_handler("SLEN", false);

static const class OKT_PLEN_Handler final: public IFFHandler<OKT_data>
{
public:
  OKT_PLEN_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, OKT_data &m) const override
  {
    m.num_orders = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    if(m.num_orders > MAX_ORDERS)
    {
      format::error("too many orders in PLEN (%u)", m.num_orders);
      return modutil::INVALID;
    }
    return modutil::SUCCESS;
  }
} PLEN_handler("PLEN", false);

static const class OKT_PATT_Handler final: public IFFHandler<OKT_data>
{
public:
  OKT_PATT_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, OKT_data &m) const override
  {
    if(len < m.num_orders)
    {
      format::error("expected %u orders in PATT but found %zu", m.num_orders, len);
      return modutil::INVALID;
    }

    if(len > MAX_ORDERS)
    {
      format::error("PATT chunk too long (%zu)", len);
      return modutil::INVALID;
    }

    if(!fread(m.orders, len, 1, fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
} PATT_handler("PATT", false);

static const class OKT_PBOD_Handler final: public IFFHandler<OKT_data>
{
public:
  OKT_PBOD_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, OKT_data &m) const override
  {
    if(len < 18) /* 2 line count + 1 row, 4 channels */
    {
      format::error("PBOD chunk length < 18.");
      return modutil::INVALID;
    }
    if(m.current_patt >= MAX_PATTERNS)
    {
      format::warning("ignoring pattern %u.", m.current_patt);
      return modutil::SUCCESS;
    }

    OKT_pattern &p = m.patterns[m.current_patt++];

    p.num_rows         = fget_u16be(fp);

    if(p.num_rows > 128)
      m.uses[FT_ROWS_OVER_128] = true;
    else
    if(p.num_rows > 64)
      m.uses[FT_ROWS_OVER_64] = true;

    p.data = new OKT_pattern::event[p.num_rows * m.num_channels];
    OKT_pattern::event *current = p.data;

    for(size_t i = 0; i < p.num_rows; i++)
    {
      for(size_t j = 0; j < m.num_channels; j++)
      {
        current->note       = fgetc(fp);
        current->instrument = fgetc(fp);
        current->effect     = fgetc(fp);
        current->param      = fgetc(fp);
        current++;
      }
    }
    if(feof(fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
} PBOD_handler("PBOD", false);

static const class OKT_SBOD_Handler final: public IFFHandler<OKT_data>
{
public:
  OKT_SBOD_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, OKT_data &m) const override
  {
    /* Ignore. */
    return modutil::SUCCESS;
  }
} SBOD_handler("SBOD", false);


static const IFF<OKT_data> OKT_parser({
  &CMOD_handler,
  &SAMP_handler,
  &SPEE_handler,
  &SLEN_handler,
  &PLEN_handler,
  &PATT_handler,
  &PBOD_handler,
  &SBOD_handler
});


class OKT_loader : modutil::loader
{
public:
  OKT_loader(): modutil::loader("OKT", "okta", "Oktalyzer") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    OKT_data m{};
    OKT_parser.max_chunk_length = 0;

    if(!fread(m.magic, 8, 1, fp))
      return modutil::FORMAT_ERROR;

    if(strncmp(m.magic, "OKTASONG", 8))
      return modutil::FORMAT_ERROR;

    total_okts++;
    modutil::error err = OKT_parser.parse_iff(fp, 0, m);
    if(err)
      return err;

    if(OKT_parser.max_chunk_length > 4*1024*1024)
      m.uses[FT_CHUNK_OVER_4_MIB] = true;

    format::line("Type",     "Oktalyzer");
    format::line("Samples",  "%u", m.num_samples);
    format::line("Channels", "%u", m.num_channels);
    format::line("Patterns", "%u", m.num_patterns);
    format::line("Orders",   "%u", m.num_orders);
    format::line("MaxChunk", "%zu", OKT_parser.max_chunk_length);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static const char *labels[] =
      {
        "Name", "Length", "LoopStart", "LoopLen", "Vol"
      };

      table::table<
        table::string<20>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4>> s_table;

      s_table.header("Samples", labels);

      for(int i = 0; i < m.num_samples; i++)
      {
        OKT_sample &s = m.samples[i];
        s_table.row(i + 1, s.name, {},
          s.length, s.repeat_start, s.repeat_length, {},
          s.volume);
      }
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", m.orders, m.num_orders);

      for(unsigned int i = 0; i < m.num_patterns; i++)
      {
        if(i >= MAX_PATTERNS)
          break;

        OKT_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::effectWide>;
        format::pattern<EVENT, 8> pattern(i, m.num_channels, p.num_rows);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        OKT_pattern::event *current = p.data;

        for(unsigned int row = 0; row < p.num_rows; row++)
        {
          for(unsigned int track = 0; track < m.num_channels; track++, current++)
          {
            format::note       a{ current->note };
            format::sample     b{ current->instrument };
            format::effectWide c{ current->effect, current->param };

            pattern.insert(EVENT(a, b, c));
          }
        }
        pattern.print();
      }
    }
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!total_okts)
      return;

    format::report("Total OKTs", total_okts);
  }
};

static const OKT_loader loader;
