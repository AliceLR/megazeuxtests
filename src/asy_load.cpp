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
 * ASYLUM loader loosely based on the public domain
 * amf2mod.c converter by Mr. P / Powersource.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

static int total_asylum = 0;


static constexpr char MAGIC[] = "ASYLUM Music Format V1.0";

static constexpr size_t MAX_INSTRUMENTS = 64;
static constexpr size_t MAX_PATTERNS = 256;
static constexpr size_t MAX_ORDERS = 256;
static constexpr size_t CHANNELS = 8;
static constexpr size_t ROWS = 64;

struct ASYLUM_header
{
  /*   0 */ char     magic[24];
  /*  24 */ uint8_t  reserved[8];
  /*  32 */ uint8_t  initial_speed;
  /*  33 */ uint8_t  initial_tempo;
  /*  34 */ uint8_t  num_samples;
  /*  35 */ uint8_t  num_patterns;
  /*  36 */ uint8_t  num_orders;
  /*  37 */ uint8_t  restart_byte; /* This is a guess, no ASYLUM AMF uses a value other than 0. */
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
};


class ASYLUM_loader : public modutil::loader
{
public:
  ASYLUM_loader(): modutil::loader("AMF : ASYLUM Music Format") {}

  virtual modutil::error load(FILE *fp) const override
  {
    ASYLUM_data m{};
    ASYLUM_header &h = m.header;

    if(!fread(h.magic, sizeof(h.magic), 1, fp))
      return modutil::READ_ERROR;

    if(memcmp(h.magic, MAGIC, sizeof(h.magic)))
      return modutil::FORMAT_ERROR;

    total_asylum++;

    /* Header */
    if(!fread(h.reserved, sizeof(h.reserved), 1, fp))
      return modutil::READ_ERROR;

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
        for(size_t track = 0; track < CHANNELS; track++)
        {
          *current = ASYLUM_event{ pos[0], pos[1], pos[2], pos[3] };
          current++;
          pos += 4;
        }
      }
    }

    /* Sample data - ignore */


    /* Print information. */

    format::line("Type",     "ASYLUM");
    format::line("Samples",  "%u", h.num_samples);
    format::line("Patterns", "%u", h.num_patterns);
    format::line("Orders",   "%u (0x%02x)", h.num_orders, h.restart_byte);

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

        using EVENT = format::event<format::note, format::sample, format::effect>;
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
            format::note   a{ current->note };
            format::sample b{ current->instrument };
            format::effect c{ current->effect, current->param };
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
