/**
 * Copyright (C) 2020 Lachesis <petrifiedrowan@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

static int total_stms = 0;


enum STM_features
{
  FT_TYPE_SONG,
  FT_TYPE_MODULE,
  FT_ORDER_EMPTY,
  FT_ORDER_INVALID,
  FT_ORDER_OVER_99,
  NUM_FEATURES
};

static const char * const FEATURE_DESC[NUM_FEATURES] =
{
  "T:Song",
  "T:Module",
  "Pat>=Count",
  "Pat>=64",
  "Pat>99",
};

static constexpr size_t MAX_ORDERS = 256;
static constexpr size_t MAX_PATTERNS = 64;

enum STM_types
{
  TYPE_SONG = 1,
  TYPE_MODULE = 2,
};

struct STM_header
{
  /*  0 */ char name[20];
  /* 20 */ char tracker[8];
  /* 28 */ uint8_t eof;
  /* 29 */ uint8_t type;
  /* 30 */ uint8_t version_maj;
  /* 31 */ uint8_t version_min;

  /* V1 fields. */
  /* 32 */ uint16_t num_instruments;
  /* 34 */ uint16_t num_orders;
  /* 36 */ uint16_t num_patterns;
  /* 38 */ uint16_t unknown;
  /* 40 */ uint8_t  tempo;
  /* 41 */ uint8_t  channels;
  /* 42 */ uint16_t pattern_size;
  /* 44 */ uint16_t unknown2;
  /* 46 */ uint16_t bytes_to_skip;

  /* V2 fields. */
  /* 32 */ //uint8_t tempo;
  /* 33 */ //uint8_t num_patterns;
  /* 34 */ uint8_t  global_volume;
  /* 35 */ uint8_t  unused[13];
};

struct STM_instrument
{
  /*  0 */ char     filename[13];
  /* 13 */ uint8_t  disk;
  /* 14 */ uint16_t segment; // Allegedly "reserved", but in practice seems to be offset in file >> 4.
  /* 16 */ uint16_t length;
  /* 18 */ uint16_t loop_start;
  /* 20 */ uint16_t loop_end;
  /* 22 */ uint8_t  default_volume;
  /* 23 */ uint8_t  reserved2;
  /* 24 */ uint16_t c2speed;
  /* 26 */ uint32_t reserved3;
  /* 30 */ uint16_t segment_length;
};

struct STM_event
{
  uint8_t note = 0;
  uint8_t instrument = 0;
  uint8_t volume = 0;
  uint8_t command = 0;
  uint8_t param = 0;

  STM_event() {}
  STM_event(FILE *fp)
  {
    uint8_t a = fgetc(fp);
    switch(a)
    {
      case 251: // All 0.
        break;

      case 252: // All 0 except note, which is -0- (?).
        note = 254;
        break;

      case 253: // All 0 except note, which is ... (?).
        note = 255;
        break;

      default:
        uint8_t b = fgetc(fp);
        uint8_t c = fgetc(fp);
        uint8_t d = fgetc(fp);
        note       = a;
        instrument = b >> 3;
        volume     = (b & 0x07) | ((c & 0xf0) >> 1);
        command    = (c & 0x0f);
        param      = d;
        break;
    }
  }
};

struct STM_pattern
{
  STM_event *events = nullptr;
  uint8_t num_channels = 0;
  uint8_t num_rows = 0;

  ~STM_pattern()
  {
    delete[] events;
  }

  void allocate()
  {
    if(num_channels && num_rows)
      events = new STM_event[num_channels * num_rows];
  }
};

struct STM_module
{
  STM_header     header;
  STM_instrument *instruments = nullptr;
  STM_pattern    *patterns = nullptr;
  uint8_t        orders[256];
  size_t         stored_orders;
  size_t         patterns_alloc;

  char name[21];
  bool uses[NUM_FEATURES];

  ~STM_module()
  {
    delete[] instruments;
    delete[] patterns;
  }
};

static modutil::error STM_read(FILE *fp)
{
  STM_module m{};
  STM_header &h = m.header;

  /**
   * Header.
   */
  if(!fread(h.name, sizeof(h.name), 1, fp))
    return modutil::FORMAT_ERROR;
  if(!fread(h.tracker, sizeof(h.tracker), 1, fp))
    return modutil::FORMAT_ERROR;

  h.eof         = fgetc(fp);
  h.type        = fgetc(fp);
  h.version_maj = fgetc(fp);
  h.version_min = fgetc(fp);
  if(feof(fp))
    return modutil::FORMAT_ERROR;

  /* This format doesn't have a proper magic, so do some basic tests on the header. */
  if(h.type != TYPE_SONG && h.type != TYPE_MODULE)
    return modutil::FORMAT_ERROR;
  for(int i = 0; i < 8; i++)
    if(!(h.tracker[i] >= 32 && h.tracker[i] <= 126))
      return modutil::FORMAT_ERROR;

  /* EOF may be non-0x1a in rare cases. */
  if(h.eof != 0x1a)
  {
    O_("wrong eof byte: %02x\n", h.eof);
    return modutil::FORMAT_ERROR;
  }

  /* libxmp checks for this STX magic string at position 60,
   * presumably to prevent false positives from S3M or STMIK files. */
  {
    long pos = ftell(fp);
    if(fseek(fp, 60, SEEK_SET))
      return modutil::SEEK_ERROR;
    char tmp[4];
    if(!fread(tmp, 4, 1, fp))
      return modutil::READ_ERROR;
    if(!memcmp(tmp, "SCRM", 4))
      return modutil::FORMAT_ERROR;
    if(fseek(fp, pos, SEEK_SET))
      return modutil::SEEK_ERROR;
  }

  total_stms++;
  if(h.version_maj == 1)
  {
    h.num_instruments = fget_u16le(fp);
    h.num_orders      = fget_u16le(fp);
    h.num_patterns    = fget_u16le(fp);
    h.unknown         = fget_u16le(fp);
    h.tempo           = fgetc(fp);
    h.channels        = fgetc(fp);
    h.pattern_size    = fget_u16le(fp);
    h.unknown2        = fget_u16le(fp);
    h.bytes_to_skip   = fget_u16le(fp);

    /* begin ??? from libxmp */
    h.tempo = (h.version_min > 0) ? ((h.tempo / 10) & 0x0f) : (h.tempo & 0x0f);
    /* end ??? */

    if(feof(fp))
      return modutil::READ_ERROR;
    if(fseek(fp, h.bytes_to_skip, SEEK_CUR))
      return modutil::SEEK_ERROR;
  }
  else

  if(h.version_maj >= 2)
  {
    h.tempo           = fgetc(fp);
    h.num_patterns    = fgetc(fp);
    h.global_volume   = fgetc(fp);
    h.num_instruments = 31;
    h.num_orders      = 128;
    h.channels        = 4;
    h.pattern_size    = 64;

    /* begin ??? from libxmp */
    h.tempo = (h.version_min == 2 && h.version_min < 21) ? ((h.tempo / 10) & 0x0f) : (h.tempo >> 4);

    if(h.version_maj == 2 && h.version_min == 0)
      h.num_orders = 64;
    /* end ??? */

    if(!fread(h.unused, sizeof(h.unused), 1, fp))
      return modutil::READ_ERROR;
  }
  else
  {
    format::error("unknown STM version %02x.%02x", h.version_maj, h.version_min);
    return modutil::BAD_VERSION;
  }

  memcpy(m.name, h.name, 20);
  m.name[20] = '\0';

  strip_module_name(m.name, 20);

  if(h.type == TYPE_SONG)
    m.uses[FT_TYPE_SONG] = true;
  if(h.type == TYPE_MODULE)
    m.uses[FT_TYPE_MODULE] = true;


  /**
   * Instruments.
   */
  m.instruments = new STM_instrument[h.num_instruments]{};
  for(size_t i = 0; i < h.num_instruments; i++)
  {
    STM_instrument &ins = m.instruments[i];
    if(!fread(ins.filename, sizeof(ins.filename), 1, fp))
    {
      format::error("read error at instrument %zu", i);
      return modutil::READ_ERROR;
    }

    ins.filename[sizeof(ins.filename) - 1] = '\0';

    ins.disk           = fgetc(fp);
    ins.segment        = fget_u16le(fp);
    ins.length         = fget_u16le(fp);
    ins.loop_start     = fget_u16le(fp);
    ins.loop_end       = fget_u16le(fp);
    ins.default_volume = fgetc(fp);
    ins.reserved2      = fgetc(fp);
    ins.c2speed        = fget_u16le(fp);
    ins.reserved3      = fget_u32le(fp);
    ins.segment_length = fget_u16le(fp);

    if(feof(fp))
    {
      format::error("read error at instrument %zu", i);
      return modutil::READ_ERROR;
    }
  }


  /**
   * Order table.
   */
  if(h.num_orders > MAX_ORDERS)
    return modutil::STM_INVALID_ORDERS;
  if(h.num_patterns > MAX_PATTERNS)
    return modutil::STM_INVALID_PATTERNS;

  m.stored_orders = h.num_orders;
  if(!fread(m.orders, h.num_orders, 1, fp))
  {
    format::error("read error at order table");
    return modutil::READ_ERROR;
  }

  size_t real_orders = 0;
  size_t patterns_alloc = h.num_patterns;
  for(size_t i = 0; i < h.num_orders; i++)
  {
    if(m.orders[i] >= 99)
    {
      if(m.orders[i] > 99)
        m.uses[FT_ORDER_OVER_99] = true;
      break;
    }
    else

    if(m.orders[i] >= 64)
    {
      // These cause undefined behavior in ST2 and should be considered invalid!
      m.uses[FT_ORDER_INVALID] = true;
    }
    else

    if(m.orders[i] >= h.num_patterns)
    {
      // These are always initialized to blank in ST2.
      m.uses[FT_ORDER_EMPTY] = true;
    }

    if(patterns_alloc < (size_t)m.orders[i] + 1)
      patterns_alloc = m.orders[i] + 1;
    real_orders++;
  }
  h.num_orders = real_orders;


  /**
   * Patterns.
   */
  m.patterns = new STM_pattern[patterns_alloc];
  for(size_t i = 0; i < h.num_patterns; i++)
  {
    STM_pattern &p = m.patterns[i];

    p.num_channels = h.channels;
    p.num_rows = h.pattern_size;
    p.allocate();

    STM_event *current = p.events;
    for(size_t row = 0; row < p.num_rows; row++)
    {
      for(size_t ch = 0; ch < p.num_channels; ch++, current++)
        *current = STM_event(fp);
    }
    if(feof(fp))
    {
      format::warning("read error at pattern %zu", i);
      break;
    }
  }


  /**
   * Print dump.
   */
  format::line("Name",     "%s", m.name);
  format::line("Type",     "STM %u.%02u", h.version_maj, h.version_min);
  format::line("Tracker",  "%8.8s", h.tracker);
  format::line("Samples",  "%u", h.num_instruments);
  format::line("Patterns", "%u", h.num_patterns);
  format::line("Orders",   "%u (%zu stored)", h.num_orders, m.stored_orders);
  format::uses(m.uses, FEATURE_DESC);

  if(Config.dump_samples)
  {
    format::line();

    static const char *labels[] =
    {
      "Filename", "Seg.", "Length", "Start", "End", "Vol", "C2Spd",
    };

    namespace table = format::table;
    table::table<
      table::string<12, encode::cp437>,
      table::spacer,
      table::number<6>,
      table::number<6>,
      table::number<6>,
      table::number<6>,
      table::spacer,
      table::number<4>,
      table::number<6>> s_table;

    s_table.header("Samples", labels);

    for(unsigned int i = 0; i < h.num_instruments; i++)
    {
      STM_instrument &ins = m.instruments[i];
      s_table.row(i + 1, ins.filename, {}, ins.segment,
        ins.length, ins.loop_start, ins.loop_end, {},
        ins.default_volume, ins.c2speed
      );
    }
  }

  if(Config.dump_patterns)
  {
    format::line();
    format::orders("Orders", m.orders, h.num_orders);

    for(unsigned int i = 0; i < h.num_patterns; i++)
    {
      STM_pattern &p = m.patterns[i];

      using EVENT = format::event<format::note<>, format::sample<>,
                                  format::volume<>, format::effectIT>;
      format::pattern<EVENT> pattern(i, p.num_channels, p.num_rows);

      if(!Config.dump_pattern_rows)
      {
        pattern.summary();
        continue;
      }
      if(!p.events)
      {
        pattern.print();
        continue;
      }

      STM_event *current = p.events;
      for(unsigned int row = 0; row < p.num_rows; row++)
      {
        for(unsigned int track = 0; track < p.num_channels; track++, current++)
        {
          format::note<>   a{ current->note, current->note != 0xFF };
          format::sample<> b{ current->instrument };
          format::volume<> c{ current->volume, current->volume < 0x41 };
          format::effectIT d{ current->command, current->param };

          pattern.insert(EVENT(a, b, c, d));
        }
      }
      pattern.print();
    }
  }

  return modutil::SUCCESS;
}


class STM_loader : modutil::loader
{
public:
  STM_loader(): modutil::loader("STM", "stm", "Scream Tracker 2") {}

  virtual modutil::error load(modutil::data state) const override
  {
    return STM_read(state.reader.unwrap()); /* FIXME: */
  }

  virtual void report() const override
  {
    if(!total_stms)
      return;

    format::report("Total STMs", total_stms);
  }
};

static const STM_loader loader;
