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

#include "Config.hpp"
#include "IFF.hpp"
#include "common.hpp"
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
  uint8_t channel_cols[8];
  bool is_empty;
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
      O_("Error     : too many patterns in SLEN (%u)\n", m.num_patterns);
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
      O_("Error     : too many orders in PLEN (%u)\n", m.num_orders);
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
      O_("Error     : expected %u orders in PATT but found %zu\n", m.num_orders, len);
      return modutil::INVALID;
    }

    if(len > MAX_ORDERS)
    {
      O_("Error     : PATT chunk too long (%zu)\n", len);
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
      O_("Error     : PBOD chunk length < 18.\n");
      return modutil::INVALID;
    }
    if(m.current_patt >= MAX_PATTERNS)
    {
      O_("Warning   : ignoring pattern %u.\n", m.current_patt);
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

    p.is_empty = true;
    for(size_t i = 0; i < p.num_rows; i++)
    {
      for(size_t j = 0; j < m.num_channels; j++)
      {
        current->note       = fgetc(fp);
        current->instrument = fgetc(fp);
        current->effect     = fgetc(fp);
        current->param      = fgetc(fp);

        switch(p.channel_cols[j])
        {
          case 0:
          case 1:
            if(current->note || current->instrument)
            {
              p.channel_cols[j] = 2;
              p.is_empty = false;
            }
            /* fall-through */
          case 2:
          case 3:
            if(current->effect)
            {
              p.channel_cols[j] = 4;
              p.is_empty = false;
            }
            break;
          default:
            break;
        }
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

static void print_headers(OKT_data &m, OKT_pattern &p)
{
  O_("        :");
  for(unsigned int chn = 0; chn < m.num_channels; chn++)
  {
    if(!p.channel_cols[chn])
      continue;

    fprintf(stderr, " #%x%*s :", chn, (p.channel_cols[chn] - 1) * 3, "");
  }
  fprintf(stderr, "\n");

  O_("------- :");
  for(size_t i = 0; i < m.num_channels; i++)
  {
    if(!p.channel_cols[i])
      continue;

    fprintf(stderr, " %.*s :", (p.channel_cols[i] * 3 - 1), "------------");
  }
  fprintf(stderr, "\n");
}

static void print_row(OKT_data &m, OKT_pattern &p, unsigned int row)
{
  O_("%6u  :", row);

  OKT_pattern::event *current = p.data + row * m.num_channels;
  for(size_t i = 0; i < m.num_channels; i++, current++)
  {
    if(!p.channel_cols[i])
      continue;

#define P_EVENT(c,v) do{ if(c) { fprintf(stderr, " %02x", v); } else { fprintf(stderr, "   "); } }while(0)

    if(p.channel_cols[i] >= 1)
      P_EVENT(current->note, current->note);
    if(p.channel_cols[i] >= 2)
      P_EVENT((current->note || current->instrument), current->instrument);
    if(p.channel_cols[i] >= 3)
      P_EVENT(current->effect, current->effect);
    if(p.channel_cols[i] >= 4)
      P_EVENT(current->effect, current->param);

    fprintf(stderr, " :");
  }
  fprintf(stderr, "\n");
}


class OKT_loader : modutil::loader
{
public:
  OKT_loader(): modutil::loader("OKT : Oktalyzer") {}

  virtual modutil::error load(FILE *fp) const override
  {
    OKT_data m{};
    OKT_parser.max_chunk_length = 0;

    if(!fread(m.magic, 8, 1, fp))
      return modutil::READ_ERROR;

    if(strncmp(m.magic, "OKTASONG", 8))
      return modutil::FORMAT_ERROR;

    total_okts++;
    modutil::error err = OKT_parser.parse_iff(fp, 0, m);
    if(err)
      return err;

    if(OKT_parser.max_chunk_length > 4*1024*1024)
      m.uses[FT_CHUNK_OVER_4_MIB] = true;

//    O_("Name    : %s\n",  m.name);
    O_("Samples : %u\n",  m.num_samples);
    O_("Channels: %u\n",  m.num_channels);
    O_("Patterns: %u\n",  m.num_patterns);
    O_("MaxChunk: %zu\n", OKT_parser.max_chunk_length);

    O_("Uses    :");
    for(int i = 0; i < NUM_FEATURES; i++)
      if(m.uses[i])
        fprintf(stderr, " %s", FEATURE_STR[i]);
    fprintf(stderr, "\n");

    if(Config.dump_samples)
    {
      // FIXME
    }

    if(Config.dump_patterns)
    {
      O_("        :\n");
      O_("Orders  :");

      for(unsigned int i = 0; i < m.num_orders; i++)
        fprintf(stderr, " %02u", m.orders[i]);
      fprintf(stderr, "\n");

      for(unsigned int i = 0; i < m.num_patterns; i++)
      {
        if(i >= MAX_PATTERNS)
          break;

        if(Config.dump_pattern_rows)
          fprintf(stderr, "\n");

        OKT_pattern &p = m.patterns[i];

        O_("Pat. %02x : %u rows\n", i, p.num_rows);

        if(Config.dump_pattern_rows)
        {
          if(p.is_empty)
          {
            O_("        : Empty pattern data.\n");
            continue;
          }

          print_headers(m, p);
          for(unsigned int row = 0; row < p.num_rows; row++)
            print_row(m, p, row);
        }
      }
    }
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!total_okts)
      return;

    fprintf(stderr, "\n");
    O_("Total OKTs          : %d\n", total_okts);
    O_("------------------- :\n");
  }
};

static const OKT_loader loader;
