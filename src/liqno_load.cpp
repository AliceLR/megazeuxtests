/**
 * Copyright (C) 2024 Lachesis <petrifiedrowan@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "modutil.hpp"

static int total_liqno = 0;

enum NO_features
{
  DUMMY,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "FIXME"
};

static const char NO_MAGIC[] = "NO\0\0";

static const unsigned int MAX_CHANNELS = 255; // todo: unknown
static const unsigned int MAX_INSTRUMENTS = 63;
static const unsigned int MAX_PATTERNS = 255;
static const unsigned int MAX_ROWS = 64;
static const unsigned int MAX_PATTERN_SIZE = MAX_CHANNELS * MAX_ROWS * 4;

struct NO_header
{
  /*  0 */ uint8_t  magic[4]; /* NO\0\0 */
  /*  4 */ uint8_t  name_length; /* 0-29 */
  /*  5 */ char     name[29 + 1];
  /* 34 */ uint8_t  num_patterns;
  /* 35 */ uint8_t  unknown_ff;
  /* 36 */ uint8_t  num_channels;
  /* 37 */ uint8_t  unknown[6];
  /* 43 */ uint8_t  order[256];
  /*299 */
};

struct NO_instrument
{
  /*  0 */ uint8_t  name_length; /* 0-30 */
  /*  1 */ char     name[30 + 1];
  /* 31 */ uint8_t  default_volume; /* 0-64 */
  /* 32 */ uint16_t rate;
  /* 34 */ uint32_t length;
  /* 38 */ uint32_t loop_start;
  /* 42 */ uint32_t loop_length;
  /* 46 */
};

struct NO_event
{
  uint8_t note;
  uint8_t instrument;
  uint8_t volume;
  uint8_t effect;
  uint8_t param;

  void load(const uint8_t *data)
  {
    uint32_t pack = mem_u32le(data);

    /* NO uses -1 for unset and counts from 0.
     * Pattern printing doesn't really like that currently, so add 1 before mask. */
    note        = ((pack >>  0u) + 1u) & 0x3fu;
    instrument  = ((pack >>  6u) + 1u) & 0x7fu;
    volume      = ((pack >> 13u) + 1u) & 0x7fu;
    effect      = ((pack >> 20u) + 1u) & 0x0fu;
    param       = pack >> 24u;
  }
};

struct NO_pattern
{
  unsigned num_rows;
  unsigned num_channels;
  std::vector<NO_event> events;

  void load(unsigned chn, const std::vector<uint8_t> &data)
  {
    num_rows = MAX_ROWS;
    num_channels = chn;
    events.resize(num_rows * num_channels);

    /* Shouldn't happen */
    if(data.size() < num_channels * num_rows * 4)
      return;

    const uint8_t *src = data.data();
    unsigned event = 0;
    for(unsigned i = 0; i < num_rows; i++)
    {
      for(unsigned j = 0; j < chn; j++)
      {
        events[event++].load(src);
        src += 4;
      }
    }
  }
};

struct NO_data
{
  NO_header     header;
  NO_instrument instruments[MAX_INSTRUMENTS];
  NO_pattern    patterns[MAX_PATTERNS];

  size_t num_orders;
  size_t num_instruments_used;
  bool uses[NUM_FEATURES];
};

class NO_loader : public modutil::loader
{
public:
  NO_loader(): modutil::loader("LIQ", "liqno", "Liquid Tracker beta") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    NO_data m{};
    NO_header &h = m.header;
    uint8_t buffer[64];
    std::vector<uint8_t> patbuf;
    size_t size_of_pattern = 0;
    size_t i;

    if(fread(buffer, 1, 4, fp) < 4)
      return modutil::FORMAT_ERROR;
    if(memcmp(buffer, NO_MAGIC, 4))
      return modutil::FORMAT_ERROR;

    total_liqno++;

    /* Header */
    if(fread(buffer + 4, 1, 43 - 4, fp) < (43 - 4))
      return modutil::READ_ERROR;

    h.name_length = MIN((int)buffer[4], 29);
    memcpy(h.name, buffer + 5, 29);
    h.name[h.name_length] = '\0';

    h.num_patterns  = buffer[34];
    h.unknown_ff    = buffer[35];
    h.num_channels  = buffer[36];
    memcpy(h.unknown, buffer + 37, 6);

    /* Orders */
    if(fread(h.order, 1, 256, fp) < 256)
    {
      format::warning("read error at order list");
      goto done;
    }
    for(i = 0; i < 256; i++)
      if(h.order[i] == 0xff)
        break;

    m.num_orders = i;

    /* Instruments */
    m.num_instruments_used = 0;
    for(i = 0; i < MAX_INSTRUMENTS; i++)
    {
      if(fread(buffer, 1, 46, fp) < 46)
      {
        format::warning("read error at instrument %zu", i);
        goto done;
      }

      NO_instrument &ins = m.instruments[i];
      ins.name_length = MIN((int)buffer[0], 30);
      memcpy(ins.name, buffer + 1, 30);
      ins.name[ins.name_length] = '\0';

      ins.default_volume  = buffer[31];
      ins.rate            = mem_u16le(buffer + 32);
      ins.length          = mem_u32le(buffer + 34);
      ins.loop_start      = mem_u32le(buffer + 38);
      ins.loop_length     = mem_u32le(buffer + 42);

      if(ins.length > 0)
        m.num_instruments_used++;
    }

    /* Patterns */
    size_of_pattern = h.num_channels * MAX_ROWS * 4;
    patbuf.resize(size_of_pattern);

    for(i = 0; i < h.num_patterns; i++)
    {
      if(fread(patbuf.data(), 1, size_of_pattern, fp) < size_of_pattern)
      {
        format::warning("read error at pattern %zu", i);
        goto done;
      }
      m.patterns[i].load(h.num_channels, patbuf);
    }

done:
    /* Print information */
    format::line("Name",      "%s", h.name);
    format::line("Type",      "Liquid Tracker NO");
    format::line("Channels",  "%d", h.num_channels);
    format::line("Patterns",  "%d", h.num_patterns);
    format::line("Orders",    "%zu", m.num_orders);
    format::line("Instr.",    "63 (%zu used)", m.num_instruments_used);
    format::line("Unknown",   "%02x", h.unknown_ff);
    format::line("Unknown 2", "%02x %02x %02x %02x %02x %02x",
      h.unknown[0], h.unknown[1], h.unknown[2], h.unknown[3], h.unknown[4], h.unknown[5]);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static const char *labels[] =
      {
        "Name", "Length", "LoopStart", "LoopEnd", "Vol", "Rate"
      };

      table::table<
        table::string<30>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<3>,
        table::number<5>> s_table;

      format::line();
      s_table.header("Instr.", labels);
      for(i = 0; i < MAX_INSTRUMENTS; i++)
      {
        NO_instrument &ins = m.instruments[i];
        s_table.row(i + 1, ins.name, {},
          ins.length, ins.loop_start, ins.loop_length, {},
          ins.default_volume, ins.rate);
      }
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", h.order, m.num_orders);

      format::line();
      if(Config.dump_pattern_rows)
      {
        format::line("Note", "Notes, instruments, volumes are all +1;"
          " the NO format has them zero-based.");
      }

      for(i = 0; i < h.num_patterns; i++)
      {
        if(!size_of_pattern)
          break;

        NO_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::volume, format::effectIT>;
        format::pattern<EVENT> pattern(i, p.num_channels, p.num_rows, size_of_pattern);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        NO_event *current = p.events.data();
        for(size_t row = 0; row < p.num_rows; row++)
        {
          for(size_t track = 0; track < p.num_channels; track++, current++)
          {
            format::note      a{ current->note };
            format::sample    b{ current->instrument };
            format::volume    c{ current->volume };
            format::effectIT  d{ current->effect, current->param };
            pattern.insert(EVENT(a, b, c, d));
          }
        }
        pattern.print();
      }
    }
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!total_liqno)
      return;

    format::report("Total Liquid (NO)", total_liqno);
  }
};

static const NO_loader loader;
