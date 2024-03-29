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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

enum MTM_features
{
  FT_E_SPEED,
  FT_E_TEMPO,
  NUM_FEATURES
};

static constexpr const char *FEATURE_STR[NUM_FEATURES] =
{
  "E:Speed",
  "E:Tempo",
};

static int total_mtms = 0;


static const int MAX_CHANNELS = 32;
//static const int MAX_TRACKS = 8191;
static const int MAX_ORDERS = 256;
static const int MAX_SAMPLES = 255;

enum MTM_effects
{
  E_ARPEGGIO,
  E_PORTAMENTO_UP,
  E_PORTAMENTO_DOWN,
  E_TONEPORTA,
  E_VIBRATO,
  E_TONEPORTA_VOLSLIDE,
  E_VIBRATO_VOLSLIDE,
  E_TREMOLO,
  E_UNUSED_8,
  E_OFFSET,
  E_VOLSLIDE,
  E_JUMP,
  E_VOLUME,
  E_BREAK,
  E_EXTENDED,
  E_SPEED,
};
enum MTM_effects_extended
{
  EX_UNUSED_0, // filter
  EX_FINE_PORTAMENTO_UP,
  EX_FINE_PORTAMENTO_DOWN,
  EX_UNUSED_3, // glissando control
  EX_UNUSED_4, // vibrato waveform
  EX_FINETUNE,
  EX_UNUSED_6, // loop
  EX_UNUSED_7, // tremolo waveform
  EX_PAN,
  EX_RETRIGGER,
  EX_FINE_VOLSLIDE_UP,
  EX_FINE_VOLSLIDE_DOWN,
  EX_NOTE_CUT,
  EX_NOTE_DELAY,
  EX_PATTERN_DELAY,
  EX_UNUSED_F, // invert loop
};

struct MTM_event
{
  uint8_t note = 0;
  uint8_t instrument = 0;
  uint8_t effect = 0;
  uint8_t param = 0;

  MTM_event() {}
  MTM_event(uint8_t a, uint8_t b, uint8_t c)
  {
    note       = (a >> 2);
    instrument = ((a & 0x03) << 4) | ((b & 0xf0) >> 4);
    effect     = (b & 0x0f);
    param      = c;
  }
};

enum MTM_instrument_flags
{
  S_16BIT = (1<<0),
};

struct MTM_instrument
{
  /*  0 */ char     name[22];
  /* 22 */ uint32_t length; /* bytes */
  /* 26 */ uint32_t loop_start; /* bytes */
  /* 30 */ uint32_t loop_end; /* bytes */
  /* 34 */ int8_t   finetune;
  /* 35 */ uint8_t  default_volume;
  /* 36 */ uint8_t  attribute;
  /* 37 */
};

struct MTM_header
{
  /*  0 */ char     magic[3];
  /*  3 */ uint8_t  version;
  /*  4 */ char     name[20];
  /* 24 */ uint16_t tracks_stored; /* num_tracks - 1 */
  /* 26 */ uint8_t  last_pattern; /* num_patterns - 1 */
  /* 27 */ uint8_t  last_order; /* num_orders - 1 */
  /* 28 */ uint16_t comment_length;
  /* 30 */ uint8_t  num_samples;
  /* 31 */ uint8_t  attribute;
  /* 32 */ uint8_t  num_rows; /* rows or "beats" per track, should be 64? */
  /* 33 */ uint8_t  num_channels;
  /* 34 */ uint8_t  panning_table[32];
  /* 66 */
};

struct MTM_data
{
  MTM_header     header;
  MTM_instrument instruments[MAX_SAMPLES];
  MTM_event      **tracks = nullptr;
  char *comment = nullptr;

  uint8_t orders[128];
  int patterns[MAX_ORDERS][MAX_CHANNELS];
  unsigned int num_tracks;
  unsigned int num_patterns;
  unsigned int num_orders;
  char name[21];
  bool uses[NUM_FEATURES];

  ~MTM_data()
  {
    if(tracks)
    {
      for(size_t i = 0; i < num_tracks; i++)
        delete[] tracks[i];
      delete[] tracks;
    }
    delete[] comment;
  }

  void allocate_tracks(uint16_t stored_tracks, uint8_t rows)
  {
    tracks = new MTM_event *[stored_tracks + 1];

    for(size_t i = 0; i <= stored_tracks; i++)
      tracks[i] = new MTM_event[rows]{};
  }
};


class MTM_loader : public modutil::loader
{
public:
  MTM_loader(): modutil::loader("MTM", "mtm", "MultiTracker") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    MTM_data m{};
    MTM_header &h = m.header;

    if(!fread(h.magic, 3, 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(h.magic, "MTM", 3))
      return modutil::FORMAT_ERROR;

    total_mtms++;

    h.version = fgetc(fp);
    if(h.version != 0x10)
    {
      format::error("unknown version %02x", h.version);
      return modutil::BAD_VERSION;
    }

    if(!fread(h.name, sizeof(h.name), 1, fp))
      return modutil::READ_ERROR;

    memcpy(m.name, h.name, sizeof(h.name));
    m.name[sizeof(h.name)] = '\0';
    strip_module_name(m.name, sizeof(m.name));

    h.tracks_stored  = fget_u16le(fp);
    h.last_pattern   = fgetc(fp);
    h.last_order     = fgetc(fp);
    h.comment_length = fget_u16le(fp);
    h.num_samples    = fgetc(fp);
    h.attribute      = fgetc(fp);
    h.num_rows       = fgetc(fp);
    h.num_channels   = fgetc(fp);

    if(!fread(h.panning_table, sizeof(h.panning_table), 1, fp))
      return modutil::READ_ERROR;

    m.num_tracks = h.tracks_stored + 1;
    m.num_patterns = h.last_pattern + 1;
    m.num_orders = h.last_order + 1;

    if(h.num_rows != 64)
      format::warning("unexpected rows per pattern %u", h.num_rows);

    /* Samples. */
    for(size_t i = 0; i < h.num_samples; i++)
    {
      MTM_instrument &ins = m.instruments[i];

      if(!fread(ins.name, sizeof(ins.name), 1, fp))
        return modutil::READ_ERROR;

      ins.length         = fget_u32le(fp);
      ins.loop_start     = fget_u32le(fp);
      ins.loop_end       = fget_u32le(fp);
      ins.finetune       = fgetc(fp);
      ins.default_volume = fgetc(fp);
      ins.attribute      = fgetc(fp);

      if(feof(fp))
        return modutil::READ_ERROR;
    }

    /* Orders. */
    if(!fread(m.orders, sizeof(m.orders), 1, fp))
      return modutil::READ_ERROR;

    /* Tracks. */
    m.allocate_tracks(m.num_tracks, h.num_rows);
    for(size_t i = 1; i < m.num_tracks; i++)
    {
      MTM_event *current = m.tracks[i];
      for(size_t row = 0; row < h.num_rows; row++, current++)
      {
        uint8_t a = fgetc(fp);
        uint8_t b = fgetc(fp);
        uint8_t c = fgetc(fp);
        *current = MTM_event(a, b, c);

        switch(current->effect)
        {
          case E_SPEED:
            if(current->param >= 0x20)
              m.uses[FT_E_TEMPO] = true;
            else
              m.uses[FT_E_SPEED] = true;
        }
      }
      if(feof(fp))
        return modutil::READ_ERROR;
    }

    /* Patterns. */
    for(size_t i = 0; i < m.num_patterns; i++)
      for(size_t j = 0; j < MAX_CHANNELS; j++)
        m.patterns[i][j] = fget_u16le(fp);

    if(feof(fp))
      return modutil::READ_ERROR;

    /* Comment. */
    if(h.comment_length)
    {
      m.comment = new char[h.comment_length + 1];
      if(!fread(m.comment, h.comment_length, 1, fp))
        return modutil::READ_ERROR;

      /* MultiTracker nul pads 40-byte lines... */
      unsigned last_line = 0;
      for(unsigned i = 0; i + 40 <= h.comment_length; i += 40)
        if(m.comment[i] != '\0')
          last_line = i + 40;

      unsigned j = 0;
      for(unsigned line = 0; line < last_line; line += 40)
      {
        for(unsigned i = 0; i < 39; i++)
        {
          if(m.comment[line + i] == '\0')
            break;
          m.comment[j++] = m.comment[line + i];
        }
        m.comment[j++] = '\n';
      }
      h.comment_length = j;
      m.comment[j] = '\0';
    }

    /* Sample data - ignore. */


    /* Print information. */

    format::line("Name",     "%s", m.name);
    format::line("Type",     "MTM %d.%d", h.version >> 4, h.version & 0x0f);
    format::line("Instr.",   "%u", h.num_samples);
    format::line("Channels", "%u", h.num_channels);
    format::line("Tracks",   "%u", m.num_tracks);
    format::line("Patterns", "%u", m.num_patterns);
    format::line("Orders",   "%u", m.num_orders);
    format::uses(m.uses, FEATURE_STR);
    format::description<40>("Desc.", m.comment, h.comment_length);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static const char *labels[] =
      {
        "Name", "Length", "LoopStart", "LoopEnd", "Vol", "Fine", "Flg"
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
        table::number<4>> i_table;

      i_table.header("Instr.", labels);

      for(size_t i = 0; i < h.num_samples; i++)
      {
        MTM_instrument &ins = m.instruments[i];
        i_table.row(i + 1, ins.name, {},
          ins.length, ins.loop_start, ins.loop_end, {},
          ins.default_volume, ins.finetune, ins.attribute);
      }
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", m.orders, m.num_orders);

      if(!Config.dump_pattern_rows)
        format::line();

      for(size_t i = 0; i < m.num_patterns; i++)
      {
        using EVENT = format::event<format::note, format::sample, format::effect>;
        format::pattern<EVENT> pattern(i, h.num_channels, h.num_rows);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          pattern.tracks(m.patterns[i]);
          continue;
        }

        for(size_t row = 0; row < h.num_rows; row++)
        {
          for(size_t track = 0; track < h.num_channels; track++)
          {
            unsigned idx = m.patterns[i][track];
            if(idx >= m.num_tracks)
              idx = 0;

            MTM_event *ev = m.tracks[idx] + row;

            format::note   a{ ev->note };
            format::sample b{ ev->instrument };
            format::effect c{ ev->effect, ev->param };

            pattern.insert(EVENT(a, b, c));
          }
        }
        pattern.print(nullptr, m.patterns[i]);
      }
    }
    return modutil::SUCCESS;
  };

  virtual void report() const override
  {
    if(!total_mtms)
      return;

    format::report("Total MTMs", total_mtms);
  };
};

static const MTM_loader loader;
