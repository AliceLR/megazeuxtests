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

#include "modutil.hpp"
#include "IFF.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static size_t num_musx = 0;


static constexpr size_t MAX_ORDERS   = 128;
static constexpr size_t MAX_PATTERNS = 64;
static constexpr size_t MAX_SAMPLES  = 36;
static constexpr size_t MAX_CHANNELS = 8;
static constexpr size_t MAX_ROWS     = 64;

enum MUSX_chunks
{
  TINF = (1<<0),
  MVOX = (1<<1),
  STER = (1<<2),
  MNAM = (1<<3),
  ANAM = (1<<4),
  MLEN = (1<<5),
  PNUM = (1<<6),
  PLEN = (1<<7),
  SEQU = (1<<8),
};

enum SAMP_chunks
{
  SNAM = (1<<0),
  SVOL = (1<<1),
  SLEN = (1<<2),
  ROFS = (1<<3),
  RLEN = (1<<4),
  SDAT = (1<<5),
};

struct MUSX_sample
{
  char     name[21];
  uint8_t  volume;
  uint32_t length;
  uint32_t loop_start;
  uint32_t loop_length;

  /* Note: Length needs to be multiple of four, probably because !Tracker
   * never bothered implementing padding for the IFF chunks and ARM famously
   * crashes software that reads from a non-aligned pointer. */

  /* Not stored, used to detect any missing subchunks. */
  uint8_t present_chunks;
};

struct MUSX_event
{
  uint8_t note;
  uint8_t sample;
  uint8_t effect;
  uint8_t param;

  MUSX_event() {}
  MUSX_event(uint32_t packed)
  {
    note   = (packed & 0xff000000) >> 24;
    sample = (packed & 0x00ff0000) >> 16;
    effect = (packed & 0x0000ff00) >> 8;
    param  = (packed & 0x000000ff);
  }
};

struct MUSX_pattern
{
  MUSX_event *events = nullptr;
  uint8_t num_rows;

  ~MUSX_pattern()
  {
    delete[] events;
  }

  void allocate(uint8_t num_channels)
  {
    events = new MUSX_event[num_channels * num_rows]{};
  }
};

struct MUSX_data
{
  uint32_t timestamp;
  uint32_t num_channels;
  uint8_t  panning[8];
  char     name[33];
  char     author[33];
  uint32_t num_orders;
  uint32_t num_patterns;
  uint8_t  orders[MAX_ORDERS];

  MUSX_sample  samples[MAX_SAMPLES];
  MUSX_pattern patterns[MAX_PATTERNS];
  size_t current_pattern;
  size_t current_sample;

  uint32_t present_chunks;
};


/**
 * SAMP chunk subchunks.
 * FIXME!!!
 */



/**
 * MUSX chunks.
 */

static const class _TINF_handler final: public IFFHandler<MUSX_data>
{
public:
  _TINF_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(m.present_chunks & TINF)
      format::warning("duplicate TINF chunk");
    m.present_chunks |= TINF;

    m.timestamp = fget_u32le(fp);
    return modutil::SUCCESS;
  }
} TINF_handler("TINF", false);

static const class _MVOX_handler final: public IFFHandler<MUSX_data>
{
public:
  _MVOX_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(m.present_chunks & MVOX)
      format::warning("duplicate MVOX chunk");
    m.present_chunks |= MVOX;

    m.num_channels = fget_u32le(fp);
    if(m.num_channels < 1 || m.num_channels > MAX_CHANNELS)
    {
      format::error("invalid number of channels %u", m.num_channels);
      return modutil::INVALID;
    }
    return modutil::SUCCESS;
  }
} MVOX_handler("MVOX", false);

static const class _STER_handler final: public IFFHandler<MUSX_data>
{
public:
  _STER_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(m.present_chunks & STER)
      format::warning("duplicate STER chunk");
    m.present_chunks |= STER;

    if(len > 8)
      len = 8;

    if(!fread(m.panning, len, 1, fp))
      return modutil::READ_ERROR;
    return modutil::SUCCESS;
  }
} STER_handler("STER", false);

static const class _MNAM_handler final: public IFFHandler<MUSX_data>
{
public:
  _MNAM_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(m.present_chunks & MNAM)
      format::warning("duplicate MNAM chunk");
    m.present_chunks |= MNAM;

    if(len > 32)
      len = 32;

    m.name[len] = '\0';
    if(!fread(m.name, len, 1, fp))
      return modutil::READ_ERROR;

    strip_module_name(m.name, len + 1);
    return modutil::SUCCESS;
  }
} MNAM_handler("MNAM", false);

static const class _ANAM_handler final: public IFFHandler<MUSX_data>
{
public:
  _ANAM_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(m.present_chunks & ANAM)
      format::warning("duplicate ANAM chunk");
    m.present_chunks |= ANAM;

    if(len > 32)
      len = 32;

    m.author[len] = '\0';
    if(!fread(m.author, len, 1, fp))
      return modutil::READ_ERROR;

    strip_module_name(m.author, len + 1);
    return modutil::SUCCESS;
  }
} ANAM_handler("ANAM", false);

static const class _MLEN_handler final: public IFFHandler<MUSX_data>
{
public:
  _MLEN_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(m.present_chunks & MLEN)
      format::warning("duplicate MLEN chunk");
    m.present_chunks |= MLEN;

    m.num_orders = fget_u32le(fp);
    if(m.num_orders > MAX_ORDERS)
    {
      format::error("invalid order count %" PRIu32, m.num_orders);
      return modutil::INVALID;
    }
    return modutil::SUCCESS;
  }
} MLEN_handler("MLEN", false);

static const class _PNUM_handler final: public IFFHandler<MUSX_data>
{
public:
  _PNUM_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(m.present_chunks & PNUM)
    {
      format::error("duplicate PNUM chunk");
      return modutil::INVALID;
    }
    m.present_chunks |= PNUM;

    m.num_patterns = fget_u32le(fp);
    if(m.num_patterns > MAX_PATTERNS)
    {
      format::error("invalid pattern count %" PRIu32, m.num_patterns);
      return modutil::INVALID;
    }
    return modutil::SUCCESS;
  }
} PNUM_handler("PNUM", false);

static const class _PLEN_handler final: public IFFHandler<MUSX_data>
{
public:
  _PLEN_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    uint8_t tmp[MAX_PATTERNS];

    if(m.present_chunks & PLEN)
    {
      format::error("duplicate PLEN chunk");
      return modutil::INVALID;
    }
    m.present_chunks |= PLEN;

    if(len > MAX_PATTERNS)
      len = MAX_PATTERNS;

    if(!fread(tmp, len, 1, fp))
      return modutil::READ_ERROR;

    for(size_t i = 0; i < len; i++)
    {
      if(tmp[i] > MAX_ROWS)
      {
        format::error("invalid row count %u for pattern %zu", tmp[i], i);
        return modutil::INVALID;
      }
      m.patterns[i].num_rows = tmp[i];
    }
    return modutil::SUCCESS;
  }
} PLEN_handler("PLEN", false);

static const class _SEQU_handler final: public IFFHandler<MUSX_data>
{
public:
  _SEQU_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(m.present_chunks & SEQU)
      format::warning("duplicate SEQU chunk");
    m.present_chunks |= SEQU;

    if(len > MAX_ORDERS)
      len = MAX_ORDERS;

    if(!fread(m.orders, len, 1, fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
} SEQU_handler("SEQU", false);

static const class _PATT_handler final: public IFFHandler<MUSX_data>
{
public:
  _PATT_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(~m.present_chunks & (MVOX | PNUM | PLEN))
    {
      format::error("invalid PATT chunk prior to MVOX, PNUM, or PLEN");
      return modutil::INVALID;
    }
    if(m.current_pattern >= m.num_patterns)
    {
      if(m.current_pattern == m.num_patterns)
        format::warning("ignoring extra patterns >= %" PRIu32, m.num_patterns);
      m.current_pattern++;
      return modutil::SUCCESS;
    }
    MUSX_pattern &p = m.patterns[m.current_pattern++];

    if(len < m.num_channels * p.num_rows * 4)
    {
      format::error("PATT chunk too short for pattern %" PRIu32, m.num_patterns);
      return modutil::INVALID;
    }

    uint8_t buffer[MAX_CHANNELS * MAX_ROWS * 4];

    if(!fread(buffer, m.num_channels * p.num_rows * 4, 1, fp))
      return modutil::READ_ERROR;

    p.allocate(m.num_channels);

    uint8_t *pos = buffer;
    MUSX_event *current = p.events;
    for(size_t row = 0; row < p.num_rows; row++)
    {
      for(size_t track = 0; track < m.num_channels; track++)
      {
        *(current++) = MUSX_event(mem_u32le(pos));
        pos += 4;
      }
    }
    return modutil::SUCCESS;
  }
} PATT_handler("PATT", false);

static const class _SAMP_handler final: public IFFHandler<MUSX_data>
{
public:
  _SAMP_handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MUSX_data &m) const override
  {
    if(m.current_sample >= MAX_SAMPLES)
    {
      if(m.current_sample == MAX_SAMPLES)
        format::warning("ignoring extra samples >=%zu", MAX_SAMPLES);
      m.current_sample++;
      return modutil::SUCCESS;
    }
    m.current_sample++;

    // FIXME load sample

    return modutil::SUCCESS;
  }
} SAMP_handler("SAMP", false);

static const IFF<MUSX_data> MUSX_parser(Endian::LITTLE, IFFPadding::BYTE,
{
  &TINF_handler,
  &MVOX_handler,
  &STER_handler,
  &MNAM_handler,
  &ANAM_handler,
  &MLEN_handler,
  &PNUM_handler,
  &PLEN_handler,
  &SEQU_handler,
  &PATT_handler,
  &SAMP_handler,
});


class MUSX_loader: modutil::loader
{
public:
  MUSX_loader(): modutil::loader("-", "musx", "!Tracker-compatible/MUSX") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    MUSX_data m{};
    uint8_t tmp[8];

    if(!fread(tmp, 8, 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(tmp, "MUSX", 4))
      return modutil::FORMAT_ERROR;

    if(file_length < 8 || mem_u32le(tmp + 4) > (size_t)file_length - 8)
      return modutil::FORMAT_ERROR;

    IFF<MUSX_data> parser = MUSX_parser;
    modutil::error err = parser.parse_iff(fp, file_length, m);
    if(err)
      return err;

    num_musx++;

    /* Were all non-PATT/SAMP chunks present? */
    if(~m.present_chunks & TINF)
      format::warning("missing TINF");
    if(~m.present_chunks & MVOX)
      format::warning("missing MVOX");
    if(~m.present_chunks & STER)
      format::warning("missing STER");
    if(~m.present_chunks & MNAM)
      format::warning("missing MNAM");
    if(~m.present_chunks & ANAM)
      format::warning("missing ANAM");
    if(~m.present_chunks & MLEN)
      format::warning("missing MLEN");
    if(~m.present_chunks & PNUM)
      format::warning("missing PNUM");
    if(~m.present_chunks & PLEN)
      format::warning("missing PLEN");
    if(~m.present_chunks & SEQU)
      format::warning("missing SEQU");


    /* Print information. */

    format::line("Name",     "%s", m.name);
    format::line("Author",   "%s", m.author);
    if(m.timestamp)
      format::line("Type",     "!Tracker-compatible/MUSX (%08x)", m.timestamp);
    else
      format::line("Type",     "!Tracker-compatible/MUSX");
    format::line("Samples",  "%zu", m.current_sample);
    format::line("Channels", "%" PRIu32, m.num_channels);
    format::line("Patterns", "%" PRIu32, m.num_patterns);
    format::line("Orders",   "%" PRIu32, m.num_orders);

    if(Config.dump_samples)
    {
      // FIXME
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", m.orders, m.num_orders);

      if(!Config.dump_pattern_rows)
        format::line();

      for(size_t i = 0; i < m.num_patterns; i++)
      {
        MUSX_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::effectWide>;
        format::pattern<EVENT> pattern(i, m.num_channels, p.num_rows);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        MUSX_event *ev = p.events;
        for(size_t row = 0; row < p.num_rows; row++)
        {
          for(size_t track = 0; track < m.num_channels; track++, ev++)
          {
            format::note       a{ ev->note };
            format::sample     b{ ev->sample };
            format::effectWide c{ ev->effect, ev->param };

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
    if(!num_musx)
      return;

    format::report("Total MUSX", num_musx);
  }
};

static const MUSX_loader loader;
