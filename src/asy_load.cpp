/**
 * Copyright (C) 2021 Lachesis <petrifiedrowan@gmail.com>
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

/**
 * ASYLUM loader loosely based on the public domain
 * amf2mod.c converter by Mr. P / Powersource.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

static int total_asylum = 0;


enum ASYLUM_features
{
  FT_FX_OVER_16,
  NUM_FEATURES
};

static constexpr const char *FEATURE_STR[NUM_FEATURES] =
{
  "X:>16"
};

static constexpr char MAGIC[] = "ASYLUM Music Format V1.0\0\0\0\0\0\0\0\0";

static constexpr size_t MAX_INSTRUMENTS = 64;
static constexpr size_t MAX_PATTERNS = 256;
static constexpr size_t MAX_ORDERS = 256;
static constexpr size_t CHANNELS = 8;
static constexpr size_t ROWS = 64;

/* Several of these fields are ignored in amf2mod, e.g. the
 * restart byte, which is relied on by the Todd Parsons AMFs.*/
struct ASYLUM_header
{
  /*   0 */ char     magic[32];
  /*  32 */ uint8_t  initial_speed;
  /*  33 */ uint8_t  initial_tempo;
  /*  34 */ uint8_t  num_samples;
  /*  35 */ uint8_t  num_patterns;
  /*  36 */ uint8_t  num_orders;
  /*  37 */ uint8_t  restart_byte;
  /*  38 */ uint8_t  orders[MAX_ORDERS];
  /* 294 */
};

struct ASYLUM_instrument
{
  /*   0 */ char     name[22];
  /*  22 */ uint8_t  finetune;
  /*  23 */ uint8_t  default_volume;
  /*  24 */ uint8_t  unknown; /* treated as transpose by libxmp...? */
  /*  25 */ uint32_t length;
  /*  29 */ uint32_t loop_start;
  /*  33 */ uint32_t loop_length;
  /*  37 */
};

struct ASYLUM_event
{
  uint8_t note;
  uint8_t instrument;
  uint8_t effect;
  uint8_t param;
};

struct ASYLUM_pattern
{
  ASYLUM_event *events = nullptr;

  ~ASYLUM_pattern()
  {
    delete[] events;
  }

  void allocate()
  {
    events = new ASYLUM_event[CHANNELS * ROWS]{};
  }
};

struct ASYLUM_data
{
  ASYLUM_header     header;
  ASYLUM_instrument instruments[MAX_INSTRUMENTS];
  ASYLUM_pattern    patterns[MAX_PATTERNS];

  bool uses[NUM_FEATURES];
};


class ASYLUM_loader : public modutil::loader
{
public:
  ASYLUM_loader(): modutil::loader("AMF", "asylum", "ASYLUM Music Format") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    ASYLUM_data m{};
    ASYLUM_header &h = m.header;

    if(!fread(h.magic, sizeof(h.magic), 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(h.magic, MAGIC, sizeof(h.magic)))
      return modutil::FORMAT_ERROR;

    total_asylum++;

    /* Header */
    h.initial_speed = fgetc(fp);
    h.initial_tempo = fgetc(fp);
    h.num_samples   = fgetc(fp);
    h.num_patterns  = fgetc(fp);
    h.num_orders    = fgetc(fp);
    h.restart_byte  = fgetc(fp);

    if(!fread(h.orders, sizeof(h.orders), 1, fp))
      return modutil::READ_ERROR;

    // The file format provides a fixed 64 instrument structs.
    if(h.num_samples > MAX_INSTRUMENTS)
    {
      format::warning("invalid number of instruments %u", h.num_samples);
      return modutil::INVALID;
    }

    /* Instruments */
    for(size_t i = 0; i < MAX_INSTRUMENTS; i++)
    {
      uint8_t buf[37];

      if(!fread(buf, sizeof(buf), 1, fp))
      {
        format::error("read error in instrument %zu", i);
        return modutil::READ_ERROR;
      }

      ASYLUM_instrument &ins = m.instruments[i];

      memcpy(ins.name, buf, sizeof(ins.name));
      ins.finetune       = buf[22];
      ins.default_volume = buf[23];
      ins.unknown        = buf[24];
      ins.length         = mem_u32le(buf + 25);
      ins.loop_start     = mem_u32le(buf + 29);
      ins.loop_length    = mem_u32le(buf + 33);
    }

    /* Patterns */
    for(size_t i = 0; i < h.num_patterns; i++)
    {
      ASYLUM_pattern &p = m.patterns[i];
      p.allocate();

      uint8_t buf[ROWS * CHANNELS * 4];

      if(!fread(buf, sizeof(buf), 1, fp))
      {
        format::error("read error in pattern %zu", i);
        return modutil::READ_ERROR;
      }

      ASYLUM_event *current = p.events;
      const uint8_t *pos = buf;

      for(size_t row = 0; row < ROWS; row++)
      {
        for(size_t track = 0; track < CHANNELS; track++, current++)
        {
          *current = ASYLUM_event{ pos[0], pos[1], pos[2], pos[3] };
          pos += 4;

          if(current->effect >= 16)
            m.uses[FT_FX_OVER_16] = true;
        }
      }
    }

    /* Sample data - ignore */


    /* Print information. */

    format::line("Type",     "ASYLUM");
    format::line("Samples",  "%u", h.num_samples);
    format::line("Patterns", "%u", h.num_patterns);
    format::line("Orders",   "%u (0x%02x)", h.num_orders, h.restart_byte);
    format::line("Speed",    "%u/%u", h.initial_speed, h.initial_tempo);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static const char *labels[] =
      {
        "Name", "Length", "LoopStart", "LoopLen", "Vol", "Fine", "???"
      };

      format::line();
      table::table<
        table::string<22>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<4>> s_table;

      s_table.header("Samples", labels);

      for(size_t i = 0; i < h.num_samples; i++)
      {
        ASYLUM_instrument &ins = m.instruments[i];
        s_table.row(i + 1, ins.name, {},
          ins.length, ins.loop_start, ins.loop_length, {},
          ins.default_volume, ins.finetune, ins.unknown);
      }
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", h.orders, h.num_orders);

      if(!Config.dump_pattern_rows)
        format::line();

      for(size_t i = 0; i < h.num_patterns; i++)
      {
        ASYLUM_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::effectWide>;
        format::pattern<EVENT> pattern(i, CHANNELS, ROWS);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        ASYLUM_event *current = p.events;
        for(size_t row = 0; row < ROWS; row++)
        {
          for(size_t track = 0; track < CHANNELS; track++, current++)
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
    if(!total_asylum)
      return;

    format::report("Total AMF/ASYLUM", total_asylum);
  }
};

static const ASYLUM_loader loader;
