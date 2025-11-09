/**
 * Copyright (C) 2020-2025 Lachesis <petrifiedrowan@gmail.com>
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

static int num_669;
static int num_composer;
static int num_unis;


static constexpr size_t MAX_SAMPLES = 64;
static constexpr size_t MAX_ORDERS = 128;
static constexpr size_t MAX_PATTERNS = 128;
static constexpr size_t NUM_ROWS = 64;
static constexpr size_t NUM_CHANNELS = 8;

struct _669_instrument
{
  /*   0 */ char     filename[13];
  /*  13 */ uint32_t length;
  /*  17 */ uint32_t loop_start;
  /*  21 */ uint32_t loop_end;
  /*  25 */
};

struct _669_event
{
  uint8_t note = 0;
  uint8_t instrument = 0;
  uint8_t volume = 0;
  uint8_t effect = 0;

  _669_event() {}
  _669_event(uint8_t a, uint8_t b, uint8_t c)
  {
    note = (a >> 2);
    instrument = ((a & 0x3) << 4) | (b >> 4);
    volume = (b & 0xf);
    effect = c;

    if(a >= 0xfe)
      note = a;
  }

  bool has_note()
  {
    return note < 0xfe;
  }

  bool has_volume()
  {
    return note < 0xff;
  }

  bool has_effect()
  {
    return effect < 0xff;
  }
};

struct _669_pattern
{
  uint8_t brk; /* from header */
  uint8_t tempo; /* from header */
  _669_event *events = nullptr;

  void allocate()
  {
    events = new _669_event[NUM_ROWS * NUM_CHANNELS]{};
  }

  ~_669_pattern()
  {
    delete[] events;
  }
};

struct _669_header
{
  /*   0 */ char     magic[2];
  /*   2 */ char     message[108];
  /* 110 */ uint8_t  num_samples;
  /* 111 */ uint8_t  num_patterns;
  /* 112 */ uint8_t  repeat_pos;
  /* 113 */ uint8_t  orders[MAX_ORDERS];
  /* 241 */ uint8_t  pattern_tempos[MAX_PATTERNS];
  /* 369 */ uint8_t  pattern_breaks[MAX_PATTERNS];
  /* 497 */
};

struct _669_data
{
  _669_header     header;
  _669_instrument instruments[MAX_SAMPLES];
  _669_pattern    patterns[MAX_PATTERNS];

  size_t num_orders;
};


class _669_loader : public modutil::loader
{
  static constexpr size_t pattern_data_size = NUM_ROWS * NUM_CHANNELS * 3;

public:
  _669_loader(): modutil::loader("669", "669", "Composer 669") {}

  virtual modutil::error load(modutil::data state) const override
  {
    vio &vf = state.reader;

    _669_data m{};
    _669_header &h = m.header;
    const char *type;
    uint8_t buffer[pattern_data_size];

    if(vf.read(h.magic, 2) < 2)
      return modutil::FORMAT_ERROR;

    if(!memcmp(h.magic, "if", 2))
    {
      type = "Composer 669";
      num_composer++;
    }
    else

    if(!memcmp(h.magic, "JN", 2))
    {
      type = "UNIS 669";
      num_unis++;
    }
    else
      return modutil::FORMAT_ERROR;

    num_669++;


    /* Header */

    if(vf.read_buffer(h.message) < sizeof(h.message))
      return modutil::READ_ERROR;

    if(vf.read(buffer, 3) < 3)
      return modutil::READ_ERROR;

    h.num_samples = buffer[0];
    h.num_patterns = buffer[1];
    h.repeat_pos = buffer[2];

    if(vf.read_buffer(h.orders) < sizeof(h.orders) ||
       vf.read_buffer(h.pattern_tempos) < sizeof(h.pattern_tempos) ||
       vf.read_buffer(h.pattern_breaks) < sizeof(h.pattern_breaks))
      return modutil::READ_ERROR;

    if(h.num_samples > MAX_SAMPLES)
    {
      format::error("sample count '%u' too high", h.num_samples);
      return modutil::INVALID;
    }
    if(h.num_patterns > MAX_PATTERNS)
    {
      format::error("pattern count '%u' too high", h.num_patterns);
      return modutil::INVALID;
    }

    size_t ord;
    for(ord = 0; ord < MAX_ORDERS; ord++)
      if(h.orders[ord] > h.num_patterns)
        break;
    m.num_orders = ord;

    /* Samples */

    for(size_t i = 0; i < h.num_samples; i++)
    {
      _669_instrument &ins = m.instruments[i];

      size_t num_in = vf.read(buffer, 25);
      if(num_in < 25)
      {
        /* Recover broken instrument by zeroing missing portion. */
        format::warning("read error in instrument %zu", i);
        memset(buffer + num_in, 0, 25 - num_in);
      }

      memcpy(ins.filename, buffer, 13);
      ins.filename[12] = '\0';

      ins.length     = mem_u32le(buffer + 13);
      ins.loop_start = mem_u32le(buffer + 17);
      ins.loop_end   = mem_u32le(buffer + 21);

      /* Don't attempt to read further instruments if EOF. */
      if(vf.eof())
        break;
    }

    /* Patterns */

    for(size_t i = 0; i < h.num_patterns; i++)
    {
      _669_pattern &p = m.patterns[i];
      p.brk = h.pattern_breaks[i];
      p.tempo = h.pattern_tempos[i];

      p.allocate();

      /* Skip read if something already hit EOF. */
      if(vf.eof())
        continue;

      size_t num_in = vf.read(buffer, pattern_data_size);
      if(num_in < pattern_data_size)
      {
        /* Recover broken pattern by zeroing missing portion. */
        format::warning("read error in pattern %zu", i);
        memset(buffer + num_in, 0, pattern_data_size - num_in);
      }

      _669_event *current = p.events;
      uint8_t *ev = buffer;
      for(size_t row = 0; row < NUM_ROWS; row++)
      {
        for(size_t track = 0; track < NUM_CHANNELS; track++, current++)
        {
          *current = _669_event(ev[0], ev[1], ev[2]);
          ev += 3;
        }
      }
    }


    /* Print information */

    format::line("Type",     "%s", type);
    format::line("Instr.",   "%u", h.num_samples);
    format::line("Patterns", "%u", h.num_patterns);
    format::line("Orders",   "%zu", m.num_orders);
    format::description<36>("Message", h.message, 108);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static constexpr const char *labels[] =
      {
        "Filename", "Length", "LoopStart", "LoopEnd"
      };

      table::table<
        table::string<12>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>> i_table;

      format::line();
      i_table.header("Instr.", labels);

      for(size_t i = 0; i < h.num_samples; i++)
      {
        _669_instrument &ins = m.instruments[i];
        i_table.row(i, ins.filename, {}, ins.length, ins.loop_start, ins.loop_end);
      }
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", h.orders, m.num_orders);
      format::line("Loop to", "%u", h.repeat_pos);

      if(!Config.dump_pattern_rows)
        format::line();

      for(size_t i = 0; i < h.num_patterns; i++)
      {
        _669_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::volume, format::effect669>;
        format::pattern<EVENT> pattern(i, NUM_CHANNELS, NUM_ROWS);

        pattern.extra("Tempo=%u, Break=%u", p.tempo, p.brk);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        _669_event *current = p.events;
        for(size_t row = 0; row < NUM_ROWS; row++)
        {
          for(size_t track = 0; track < NUM_CHANNELS; track++, current++)
          {
            format::note      a{ current->note, current->has_note() };
            format::sample    b{ current->instrument, current->has_note() };
            format::volume    c{ current->volume, current->has_volume() };
            format::effect669 d{ current->effect, current->has_effect() };

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
    if(!num_669)
      return;

    format::report("Total 669s", num_669);
    if(num_composer)
      format::reportline("Composer 669s", "%d", num_composer);
    if(num_unis)
      format::reportline("UNIS 669",      "%d", num_unis);
  }
};

static const _669_loader loader;
