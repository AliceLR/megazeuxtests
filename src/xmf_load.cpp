/**
 * Copyright (C) 2023 Lachesis <petrifiedrowan@gmail.com>
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
 * Imperium Galactica XMF loader. Reverse engineered with a hex editor.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "modutil.hpp"

static int total_xmf = 0;


enum XMF_features
{
  FT_E_ARPEGGIO,
  FT_E_PORTA_UP,
  FT_E_PORTA_DN,
  FT_E_TONEPORTA,
  FT_E_VIBRATO,
  FT_E_VOLSLIDE_TONEPORTA,
  FT_E_VOLSLIDE_VIBRATO,
  FT_E_TREMOLO,
  FT_E_8,
  FT_E_OFFSET,
  FT_E_VOLSLIDE,
  FT_E_JUMP,
  FT_E_VOLUME,
  FT_E_BREAK,
  FT_E_EXTENDED,
  FT_E_SPEED,
  FT_E_BPM,
  FT_E_PAN,
  FT_E_PAN_OVER_16,
  FT_E_UNKNOWN,
  NUM_FEATURES
};

static constexpr const char *FEATURE_STR[NUM_FEATURES] =
{
  "E:Arpeggio",
  "E:PortaUp",
  "E:PortaDn",
  "E:Toneporta",
  "E:Vibrato",
  "E:VolPorta",
  "E:VolVibrato",
  "E:Tremolo",
  "E:8",
  "E:Offset",
  "E:Volslide",
  "E:Jump",
  "E:Volume",
  "E:Break",
  "E:Ext",
  "E:Speed",
  "E:BPM",
  "E:Pan",
  "E:Pan>f",
  "E:unknown",
};

static constexpr size_t MAX_INSTRUMENTS = 256;
static constexpr size_t MAX_PATTERNS = 256;
static constexpr size_t MAX_ORDERS = 256;
static constexpr size_t MAX_CHANNELS = 32; // guessed; nothing prevents higher values.
static constexpr size_t ROWS = 64;

enum XMF_effects
{
  /* unused in Imperium Galactica */
  E_ARPEGGIO,
  E_PORTA_UP,
  E_PORTA_DN,
  E_TONEPORTA,
  E_VIBRATO,
  E_VOLSLIDE_TONEPORTA,
  E_VOLSLIDE_VIBRATO,
  E_TREMOLO,
  E_8,
  E_OFFSET,
  /* end unused */
  E_VOLSLIDE,
  E_JUMP,      // may actually be pan if this is really based on Ultra Tracker.
  E_VOLUME,
  E_BREAK,
  E_EXTENDED,
  E_SPEED_BPM, // <0x20: speed, >=0x20: BPM
  E_PAN,       // GUS range
};

enum XMF_flags
{
  S_LOOP  = (1 << 3),
  S_16BIT = (1 << 4), // BIDI? guessed.
};

struct XMF_instrument
{
  /* calc*/ uint32_t length;

  /*   0 */ uint32_t loop_start;     // 24-bit; from start of sample
  /*   3 */ uint32_t loop_end;       // 24-bit; from start of sample
  /*   6 */ uint32_t data_start;     // 24-bit; from start of sample data area
  /*   9 */ uint32_t data_end;       // 24-bit; from start of sample data area
  /*  12 */ uint8_t  default_volume; // volbase 255
  /*  13 */ uint8_t  flags;
  /*  14 */ uint16_t sample_rate;    // usually 8363
  /*  16 */
};

struct XMF_sequence
{
  /*   0 */ uint8_t  orders[MAX_ORDERS];
  /* 256 */ uint8_t  num_channels;
  /* 257 */ uint8_t  num_patterns;
  /* 258 */ uint8_t  default_panning[MAX_CHANNELS];
  /* 258 + num_channels */
};

struct XMF_event
{
  uint8_t note;
  uint8_t instrument;
  uint8_t effect_1;
  uint8_t effect_2;
  uint8_t param_2; // this is not a mistake. wtf?
  uint8_t param_1;
};

struct XMF_pattern
{
  std::vector<uint8_t> events;

  static constexpr size_t size(uint8_t num_channels)
  {
    static_assert(sizeof(XMF_event) == 6, "wtf");
    return num_channels * ROWS * sizeof(XMF_event);
  }

  void initialize(uint8_t num_channels)
  {
    events.resize(size(num_channels));
  }

  XMF_event *get_events()
  {
    return reinterpret_cast<XMF_event *>(events.data());
  }
};

struct XMF_data
{
  /* Header is one byte with the value 0x03 */
  XMF_instrument    instruments[MAX_INSTRUMENTS];
  XMF_sequence      sequence;
  XMF_pattern       patterns[MAX_PATTERNS];

  unsigned num_instruments;
  unsigned num_orders;

  bool uses[NUM_FEATURES];
};


static enum XMF_features get_effect_feature(uint8_t effect, uint8_t param)
{
  switch(effect)
  {
  case E_ARPEGGIO:           return FT_E_ARPEGGIO;
  case E_PORTA_UP:           return FT_E_PORTA_UP;
  case E_PORTA_DN:           return FT_E_PORTA_DN;
  case E_TONEPORTA:          return FT_E_TONEPORTA;
  case E_VIBRATO:            return FT_E_VIBRATO;
  case E_VOLSLIDE_TONEPORTA: return FT_E_VOLSLIDE_TONEPORTA;
  case E_VOLSLIDE_VIBRATO:   return FT_E_VOLSLIDE_VIBRATO;
  case E_TREMOLO:            return FT_E_TREMOLO;
  case E_8:                  return FT_E_8;
  case E_OFFSET:             return FT_E_OFFSET;
  case E_VOLSLIDE:           return FT_E_VOLSLIDE;
  case E_JUMP:               return FT_E_JUMP;
  case E_VOLUME:             return FT_E_VOLUME;
  case E_BREAK:              return FT_E_BREAK;
  case E_EXTENDED:           return FT_E_EXTENDED;
  case E_SPEED_BPM:
    if(param < 0x20)         return FT_E_SPEED;
    else                     return FT_E_BPM;
  case E_PAN:                return FT_E_PAN;
  default:
    return FT_E_UNKNOWN;
  }
}

static void check_effect_features(XMF_data &m, uint8_t effect, uint8_t param)
{
  if(effect && param)
  {
    m.uses[get_effect_feature(effect, param)] = true;

    if(effect == 0x10 && param >= 0x10)
      m.uses[FT_E_PAN_OVER_16] = true;
  }
}

static void check_event_features(XMF_data &m, const XMF_event *event)
{
  check_effect_features(m, event->effect_1, event->param_1);
  check_effect_features(m, event->effect_2, event->param_2);
}


class XMF_loader : public modutil::loader
{
public:
  XMF_loader(): modutil::loader("XMF", "imperium", "Imperium Galactica") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    XMF_data m{};
    XMF_sequence &h = m.sequence;

    if(fgetc(fp) != 0x03)
      return modutil::FORMAT_ERROR;

    /* Instruments */
    /* FIXME: better format checking once the sample junk is figured out. */
    for(size_t i = 0; i < MAX_INSTRUMENTS; i++)
    {
      XMF_instrument &ins = m.instruments[i];
      uint8_t tmp[16];

      if(fread(tmp, 1, 16, fp) < 16)
        return modutil::FORMAT_ERROR;

      ins.loop_start     = mem_u24le(tmp + 0);
      ins.loop_end       = mem_u24le(tmp + 3);
      ins.data_start     = mem_u24le(tmp + 6);
      ins.data_end       = mem_u24le(tmp + 9);
      ins.default_volume = tmp[12];
      ins.flags          = tmp[13];
      ins.sample_rate    = mem_u16le(tmp + 14);

      /* Data end should always be >= data start.
       * Most data offsets are word-padded, but not always...
       * In two files (SAMPLE.XMF and URES.XMF) these start well past the end of the file! */
      if(ins.data_start > ins.data_end)
        return modutil::FORMAT_ERROR;

      ins.length = ins.data_end - ins.data_start;

      /* Loops are always well-formed. */
      if(ins.loop_end && (ins.loop_start > ins.loop_end || ins.loop_end > ins.length))
        return modutil::FORMAT_ERROR;

      /* TODO: samples never overlap? */

      /* TODO: better way of determining usage? */
      if(ins.length && ins.sample_rate)
        m.num_instruments = i + 1;
    }

    total_xmf++;

    /* Sequence */
    if(fread(h.orders, 1, 256, fp) < 256)
      return modutil::READ_ERROR;

    h.num_channels = fgetc(fp) + 1;
    h.num_patterns = fgetc(fp) + 1;
    if(ferror(fp))
      return modutil::READ_ERROR;

    if(h.num_channels < 1 || h.num_channels > MAX_CHANNELS)
      return modutil::INVALID;

    if(fread(h.default_panning, 1, h.num_channels, fp) < h.num_channels)
      return modutil::READ_ERROR;

    for(size_t i = 0; i < 256; i++)
    {
      if(h.orders[i] == 0xff)
        break;
      m.num_orders++;
    }

    /* Patterns */
    for(size_t i = 0; i < h.num_patterns; i++)
    {
      XMF_pattern &p = m.patterns[i];
      p.initialize(h.num_channels);

      size_t sz = XMF_pattern::size(h.num_channels);

      if(fread(p.events.data(), 1, sz, fp) < sz)
        return modutil::READ_ERROR;

      XMF_event *current = p.get_events();

      for(size_t row = 0; row < ROWS; row++)
        for(size_t track = 0; track < h.num_channels; track++, current++)
          check_event_features(m, current);
    }

    /* Sample data - ignore */


    /* Print information. */

    format::line("Type",     "Imperium Galactica");
    format::line("Tracks",   "%u", h.num_channels);
    format::line("Samples",  "%u", m.num_instruments);
    format::line("Patterns", "%u", h.num_patterns);
    format::line("Orders",   "%u", m.num_orders);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static const char *labels[] =
      {
        "Length", "LoopSt.", "LoopEnd", "DataSt.", "DataEnd", "Vol", "Flg.", "Rate"
      };

      format::line();
      table::table<
        table::number<8>,
        table::number<8>,
        table::number<8>,
        table::spacer,
        table::number<8>,
        table::number<8>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<5>> s_table;

      s_table.header("Samples", labels);

      for(size_t i = 0; i < m.num_instruments; i++)
      {
        XMF_instrument &ins = m.instruments[i];
        s_table.row(i + 1,
          ins.length, ins.loop_start, ins.loop_end, {},
          ins.data_start, ins.data_end, {},
          ins.default_volume, ins.flags, ins.sample_rate);
      }
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", h.orders, m.num_orders);

      if(!Config.dump_pattern_rows)
        format::line();

      for(size_t i = 0; i < h.num_patterns; i++)
      {
        XMF_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::effectWide, format::effectWide>;
        format::pattern<EVENT> pattern(i, h.num_channels, ROWS);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        XMF_event *current = p.get_events();
        for(size_t row = 0; row < ROWS; row++)
        {
          for(size_t track = 0; track < h.num_channels; track++, current++)
          {
            format::note       a{ current->note };
            format::sample     b{ current->instrument };
            format::effectWide c{ current->effect_1, current->param_1 };
            format::effectWide d{ current->effect_2, current->param_2 };
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
    if(!total_xmf)
      return;

    format::report("Total Imperium Galactica", total_xmf);
  }
};

static const XMF_loader loader;
