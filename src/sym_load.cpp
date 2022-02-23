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

#include "modutil.hpp"
#include "Bitstream.hpp"
#include "LZW.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static size_t num_syms = 0;


enum SYM_features
{
  FT_NONE,

  FT_SAMPLE_VIDC,
  FT_SAMPLE_LZW,
  FT_SAMPLE_LINEAR,
  FT_SAMPLE_LINEAR_16,
  FT_SAMPLE_SIGMA_DELTA_LINEAR,
  FT_SAMPLE_SIGMA_DELTA_VIDC,

  FT_E_ARPEGGIO_VOLSLIDE_UP,
  FT_E_PORTA_UP_VOLSLIDE_UP,
  FT_E_PORTA_DN_VOLSLIDE_UP,
  FT_E_TONE_PORTA,
  FT_E_VIBRATO,
  FT_E_TONE_PORTA_VOLSLIDE,
  FT_E_VIBRATO_VOLSLIDE,
  FT_E_TREMOLO,
  FT_E_OFFSET,
  FT_E_OFFSET_HIGH,
  FT_E_VOLSLIDE_FINE_PORTA_UP,
  FT_E_JUMP,
  FT_E_VOLUME,
  FT_E_BREAK,
  FT_E_SPEED,
  FT_E_FILTER_CTRL,
  FT_E_FINE_PORTA_UP_FINE_VOLSLIDE_UP,
  FT_E_FINE_PORTA_DN_FINE_VOLSLIDE_UP,
  FT_E_GLISSANDO_CTRL,
  FT_E_VIBRATO_WAVEFORM,
  FT_E_FINETUNE,
  FT_E_LOOP,
  FT_E_TREMOLO_WAVEFORM,
  FT_E_RETRIGGER_NOTE,
  FT_E_FINE_PORTA_UP_FINE_VOLSLIDE_DN,
  FT_E_FINE_PORTA_DN_FINE_VOLSLIDE_DN,
  FT_E_NOTE_CUT,
  FT_E_NOTE_DELAY,
  FT_E_PATTERN_DELAY,
  FT_E_INVERT_LOOP,
  FT_E_ARPEGGIO_VOLSLIDE_DN,
  FT_E_PORTA_UP_VOLSLIDE_DN,
  FT_E_PORTA_DN_VOLSLIDE_DN,
  FT_E_VOLSLIDE_FINE_PORTA_DN,
  FT_E_LINE_JUMP,
  FT_E_TEMPO,
  FT_E_SET_STEREO,
  FT_E_SONG_UPCALL,
  FT_E_UNSET_SAMPLE_REPEAT,
  NUM_FEATURES
};

static constexpr const char *FEATURE_STR[] =
{
  "",
  "S:Log",
  "S:LZW",
  "S:8",
  "S:16",
  "S:SigmaLin",
  "S:SigmaLog",

  "E:Arpeggio+",
  "E:PortaUp+",
  "E:PortaDn+",
  "E:Tporta",
  "E:Vib",
  "E:TportaVS",
  "E:VibVS",
  "E:Tremolo",
  "E:Offset",
  "E:OffsetHi",
  "E:VolslideP+",
  "E:Jump",
  "E:Vol",
  "E:Break",
  "E:Speed",
  "E:Filter",
  "E:FPortaUp+",
  "E:FPortaDn+",
  "E:Glissando",
  "E:VibWF",
  "E:Finetune",
  "E:Loop",
  "E:TremoloWF",
  "E:Retrig",
  "E:FPortaUp-",
  "E:FPortaDn-",
  "E:Cut",
  "E:Delay",
  "E:PattDelay",
  "E:InvLoop",
  "E:Arpeggio-",
  "E:PortaUp-",
  "E:PortaDn-",
  "E:VolslideP-",
  "E:LineJump",
  "E:Tempo",
  "E:Stereo",
  "E:Upcall",
  "E:UnsetLoop",
};

static constexpr char MAGIC[] = "\x02\x01\x13\x13\x14\x12\x01\x0B"; /* BASSTRAK */
static constexpr int MAX_CHANNELS = 8;
static constexpr int MAX_SAMPLES = 63;
static constexpr int NUM_ROWS = 64;

enum SYM_packing
{
  UNPACKED,
  LZW,
};

enum SYM_effects
{
  E_ARPEGGIO_VOLSLIDE_UP,
  E_PORTA_UP_VOLSLIDE_UP,
  E_PORTA_DN_VOLSLIDE_UP,
  E_TONE_PORTA,
  E_VIBRATO,
  E_TONE_PORTA_VOLSLIDE,
  E_VIBRATO_VOLSLIDE,
  E_TREMOLO,
  E_UNUSED_08,
  E_OFFSET,
  E_VOLSLIDE_FINE_PORTA_UP,
  E_JUMP,
  E_VOLUME,
  E_BREAK,
  E_UNUSED_0E,
  E_SPEED,

  E_FILTER_CTRL,
  E_FINE_PORTA_UP_FINE_VOLSLIDE_UP,
  E_FINE_PORTA_DN_FINE_VOLSLIDE_UP,
  E_GLISSANDO_CTRL,
  E_VIBRATO_WAVEFORM,
  E_FINETUNE,
  E_LOOP,
  E_TREMOLO_WAVEFORM,
  E_UNUSED_18,
  E_RETRIGGER_NOTE,
  E_FINE_PORTA_UP_FINE_VOLSLIDE_DN,
  E_FINE_PORTA_DN_FINE_VOLSLIDE_DN,
  E_NOTE_CUT,
  E_NOTE_DELAY,
  E_PATTERN_DELAY,
  E_INVERT_LOOP,

  E_ARPEGGIO_VOLSLIDE_DN,
  E_PORTA_UP_VOLSLIDE_DN,
  E_PORTA_DN_VOLSLIDE_DN,
  E_UNUSED_23,
  E_UNUSED_24,
  E_UNUSED_25,
  E_UNUSED_26,
  E_UNUSED_27,
  E_UNUSED_28,
  E_UNUSED_29,
  E_VOLSLIDE_FINE_PORTA_DN,
  E_LINE_JUMP,
  E_UNUSED_2C,
  E_UNUSED_2D,
  E_UNUSED_2E,
  E_TEMPO,

  E_SET_STEREO,
  E_SONG_UPCALL,
  E_UNSET_SAMPLE_REPEAT,
};

struct SYM_header
{
  char     magic[8];
  uint8_t  version;
  uint8_t  num_channels;
  uint16_t num_orders;
  uint16_t num_tracks;
  uint32_t text_length; /* Stored 24-bit. */

  uint8_t  name_length;
  char     name[255]; /* Stored as name_length bytes. */
  uint8_t  effects_allowed[8];

  uint8_t order_packing;
  uint8_t track_packing;
  uint8_t text_packing;
};

struct SYM_instrument
{
  enum SYM_instrument_packing
  {
    UNCOMPRESSED_VIDC,
    LZW_DELTA_LINEAR,
    UNCOMPRESSED_LINEAR,
    UNCOMPRESSED_LINEAR_16,
    SIGMA_DELTA_LINEAR,
    SIGMA_DELTA_VIDC,
  };

  char     name[64];
  uint8_t  name_length; /* Stored in header. Bits 6 and 7 are flags. */
  uint32_t length;      /* Stored as 24-bit in header. */
  uint32_t loop_start;  /* Stored as 24-bit. */
  uint32_t loop_length; /* Stored as 24-bit. */
  uint8_t  volume;
  int8_t   finetune;
  uint8_t  packing;
};

struct SYM_event
{
  uint8_t  note = 0;
  uint8_t  instrument = 0;
  uint8_t  effect = 0;
  uint16_t param = 0;

  SYM_event() {}
  SYM_event(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
  {
    note       = (a & 0x3f);
    instrument = ((b & 0x1f) << 2) | (a >> 6);
    effect     = ((c & 0x0f) << 2) | (b >> 6);
    param      = (d << 4) | (c >> 4);
  }
};

struct SYM_track
{
  SYM_event *events = nullptr;

  ~SYM_track()
  {
    delete[] events;
  }

  void allocate(uint8_t rows)
  {
    events = new SYM_event[rows]{};
  }
};

struct SYM_order
{
  int tracks[8];
};

struct SYM_data
{
  SYM_header     header;
  SYM_instrument instruments[63];
  SYM_track      *tracks = nullptr;
  SYM_order      *orders = nullptr;
  char           *text = nullptr;

  SYM_track blank_track;

  uint8_t *buffer = nullptr;
  size_t buffer_size;
  size_t total_track_size;
  size_t total_sequence_size;

  char name[256];
  size_t num_samples;
  bool uses[NUM_FEATURES];

  ~SYM_data()
  {
    delete[] tracks;
    delete[] orders;
    delete[] text;
    delete[] buffer;
  }

  void allocate()
  {
    tracks = new SYM_track[header.num_tracks + 1]{};
    orders = new SYM_order[header.num_orders]{};
    text   = new char[header.text_length + 1];

    /* Intermediate buffer for depacking since most LZW areas
     * are unskippable. */
    buffer_size = 0;
    for(size_t i = 0; i < MAX_SAMPLES; i++)
      if(buffer_size < instruments[i].length)
        buffer_size = instruments[i].length;

    total_track_size = 4 * NUM_ROWS * header.num_tracks;
    if(buffer_size < total_track_size)
      buffer_size = total_track_size;

    total_sequence_size = sizeof(uint16_t) * header.num_channels * header.num_orders;
    if(buffer_size < total_sequence_size)
      buffer_size = total_sequence_size;

    buffer = new uint8_t[buffer_size];
  }
};


class SigmaDelta
{
public:
  /**
   * Based on the sigma delta sample decoder from OpenMPT by Saga Musix.
   */
  static modutil::error read(uint8_t *dest, size_t dest_len, FILE *fp)
  {
    size_t pos = 0;
    size_t runlength = 0;
    uint8_t bits = 8;

    if(!dest_len)
      return modutil::SUCCESS;

    Bitstream bs(fp, dest_len);

    size_t max_runlength = fgetc(fp); // Doesn't count towards alignment for some reason.
    uint8_t accumulator = bs.read(8);
    dest[pos++] = accumulator;

    while(pos < dest_len)
    {
      int value = bs.read(bits);
      if(value < 0)
        return modutil::READ_ERROR;

      // Expand bitwidth.
      if(value == 0)
      {
        if(bits >= 9)
          break; // ??

        bits++;
        runlength = 0;
        continue;
      }

      if(value & 1)
        accumulator -= (value >> 1);
      else
        accumulator += (value >> 1);

      dest[pos++] = accumulator;

      // High bit set -> reset run length.
      if(value >> (bits - 1))
      {
        runlength = 0;
      }
      else

      if(++runlength >= max_runlength)
      {
        if(bits > 1)
          bits--;
        runlength = 0;
      }
    }

    /* Digital Symphony aligns packed stream lengths to 4 bytes. */
    size_t total = bs.num_read;
    while(total & 3)
    {
      fgetc(fp);
      total++;
    }

    return modutil::SUCCESS;
  }
};

static SYM_features get_effect_feature(const SYM_event &ev)
{
  switch(ev.effect)
  {
    case E_ARPEGGIO_VOLSLIDE_UP:           return FT_E_ARPEGGIO_VOLSLIDE_UP;
    case E_PORTA_UP_VOLSLIDE_UP:           return FT_E_PORTA_UP_VOLSLIDE_UP;
    case E_PORTA_DN_VOLSLIDE_UP:           return FT_E_PORTA_DN_VOLSLIDE_UP;
    case E_TONE_PORTA:                     return FT_E_TONE_PORTA;
    case E_VIBRATO:                        return FT_E_VIBRATO;
    case E_TONE_PORTA_VOLSLIDE:            return FT_E_TONE_PORTA_VOLSLIDE;
    case E_VIBRATO_VOLSLIDE:               return FT_E_VIBRATO_VOLSLIDE;
    case E_TREMOLO:                        return FT_E_TREMOLO;
    case E_OFFSET:                         return (ev.param >= 0x200) ? FT_E_OFFSET_HIGH : FT_E_OFFSET;
    case E_VOLSLIDE_FINE_PORTA_UP:         return FT_E_VOLSLIDE_FINE_PORTA_UP;
    case E_JUMP:                           return FT_E_JUMP;
    case E_VOLUME:                         return FT_E_VOLUME;
    case E_BREAK:                          return FT_E_BREAK;
    case E_SPEED:                          return FT_E_SPEED;
    case E_FILTER_CTRL:                    return FT_E_FILTER_CTRL;
    case E_FINE_PORTA_UP_FINE_VOLSLIDE_UP: return FT_E_FINE_PORTA_UP_FINE_VOLSLIDE_UP;
    case E_FINE_PORTA_DN_FINE_VOLSLIDE_UP: return FT_E_FINE_PORTA_DN_FINE_VOLSLIDE_UP;
    case E_GLISSANDO_CTRL:                 return FT_E_GLISSANDO_CTRL;
    case E_VIBRATO_WAVEFORM:               return FT_E_VIBRATO_WAVEFORM;
    case E_FINETUNE:                       return FT_E_FINETUNE;
    case E_LOOP:                           return FT_E_LOOP;
    case E_TREMOLO_WAVEFORM:               return FT_E_TREMOLO_WAVEFORM;
    case E_RETRIGGER_NOTE:                 return FT_E_RETRIGGER_NOTE;
    case E_FINE_PORTA_UP_FINE_VOLSLIDE_DN: return FT_E_FINE_PORTA_UP_FINE_VOLSLIDE_DN;
    case E_FINE_PORTA_DN_FINE_VOLSLIDE_DN: return FT_E_FINE_PORTA_DN_FINE_VOLSLIDE_DN;
    case E_NOTE_CUT:                       return FT_E_NOTE_CUT;
    case E_NOTE_DELAY:                     return FT_E_NOTE_DELAY;
    case E_PATTERN_DELAY:                  return FT_E_PATTERN_DELAY;
    case E_INVERT_LOOP:                    return FT_E_INVERT_LOOP;
    case E_ARPEGGIO_VOLSLIDE_DN:           return FT_E_ARPEGGIO_VOLSLIDE_DN;
    case E_PORTA_UP_VOLSLIDE_DN:           return FT_E_PORTA_UP_VOLSLIDE_DN;
    case E_PORTA_DN_VOLSLIDE_DN:           return FT_E_PORTA_DN_VOLSLIDE_DN;
    case E_VOLSLIDE_FINE_PORTA_DN:         return FT_E_VOLSLIDE_FINE_PORTA_DN;
    case E_LINE_JUMP:                      return FT_E_LINE_JUMP;
    case E_TEMPO:                          return FT_E_TEMPO;
    case E_SET_STEREO:                     return FT_E_SET_STEREO;
    case E_SONG_UPCALL:                    return FT_E_SONG_UPCALL;
    case E_UNSET_SAMPLE_REPEAT:            return FT_E_UNSET_SAMPLE_REPEAT;
  }
  return FT_NONE;
}

static void check_event_features(SYM_data &m, const SYM_event &ev)
{
  SYM_features effect_feature = get_effect_feature(ev);
  if(effect_feature && (ev.effect || ev.param))
    m.uses[effect_feature] = true;
}


class SYM_loader: public modutil::loader
{
public:
  SYM_loader(): modutil::loader("-", "sym", "Digital Symphony") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    SYM_data m{};
    SYM_header &h = m.header;

    if(!fread(h.magic, sizeof(h.magic), 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(h.magic, MAGIC, sizeof(h.magic)))
      return modutil::FORMAT_ERROR;

    num_syms++;

    h.version      = fgetc(fp);
    h.num_channels = fgetc(fp);
    h.num_orders   = fget_u16le(fp);
    h.num_tracks   = fget_u16le(fp);
    h.text_length  = fget_u24le(fp);

    if(h.num_channels > MAX_CHANNELS)
    {
      format::error("invalid number of channels %u > 8", h.num_channels);
      return modutil::INVALID;
    }

    for(size_t i = 0; i < MAX_SAMPLES; i++)
    {
      SYM_instrument &ins = m.instruments[i];
      ins.name_length = fgetc(fp);
      if(~ins.name_length & 0x80)
      {
        ins.length = fget_u24le(fp) << 1;
        m.num_samples++;
      }
    }

    h.name_length = fgetc(fp);
    if(h.name_length)
    {
      if(!fread(h.name, h.name_length, 1, fp))
        return modutil::READ_ERROR;

      memcpy(m.name, h.name, h.name_length);
      m.name[h.name_length] = '\0';
      strip_module_name(m.name, h.name_length + 1);
    }

    if(!fread(h.effects_allowed, sizeof(h.effects_allowed), 1, fp))
      return modutil::READ_ERROR;

    /* Initialize data structures and temporary buffer. */
    m.allocate();

    /**
     * Orders.
     */
    if(h.num_orders)
    {
      h.order_packing = fgetc(fp);
      if(feof(fp) || h.order_packing > 1)
      {
        format::error("invalid order packing type %u", h.order_packing);
        return modutil::INVALID;
      }

      if(h.order_packing)
      {
        if(LZW_read(m.buffer, m.total_sequence_size, m.total_sequence_size, LZW_FLAGS_SYM, fp) != 0)
          return modutil::BAD_PACKING;
      }
      else
      {
        if(!fread(m.buffer, m.total_sequence_size, 1, fp))
          return modutil::READ_ERROR;
      }

      uint8_t *pos = m.buffer;
      for(size_t i = 0; i < h.num_orders; i++)
      {
        for(size_t j = 0; j < h.num_channels; j++, pos += 2)
        {
          uint16_t trk = mem_u16le(pos);
          m.orders[i].tracks[j] = trk;
        }
      }
    }

    /**
     * Tracks.
     */
    if(h.num_tracks)
    {
      static constexpr size_t TRACK_BLOCK_SIZE = 4 * 64 * 2000;

      for(size_t i = 0; i < m.total_track_size; i += TRACK_BLOCK_SIZE)
      {
        size_t block_size = MIN(m.total_track_size - i, TRACK_BLOCK_SIZE);

        h.track_packing = fgetc(fp);
        if(feof(fp) || h.track_packing > 1)
        {
          format::error("invalid track packing type %u", h.track_packing);
          return modutil::INVALID;
        }

        if(h.track_packing)
        {
          if(LZW_read(m.buffer + i, block_size, block_size, LZW_FLAGS_SYM, fp) != 0)
            return modutil::BAD_PACKING;
        }
        else
        {
          if(!fread(m.buffer + i, block_size, 1, fp))
            return modutil::READ_ERROR;
        }
      }

      uint8_t *pos = m.buffer;
      for(size_t i = 0; i < h.num_tracks; i++)
      {
        SYM_track &t = m.tracks[i];
        t.allocate(NUM_ROWS);

        for(size_t row = 0; row < NUM_ROWS; row++, pos += 4)
        {
          t.events[row] = SYM_event(pos[0], pos[1], pos[2], pos[3]);
          check_event_features(m, t.events[row]);
        }
      }
    }
    m.blank_track.allocate(NUM_ROWS);

    /**
     * Samples.
     */
    for(size_t i = 0; i < MAX_SAMPLES; i++)
    {
      SYM_instrument &ins = m.instruments[i];

      if(ins.name_length & 0x3f)
        if(!fread(ins.name, ins.name_length & 0x3f, 1, fp))
          return modutil::READ_ERROR;

      if(ins.name_length & 0x80)
        continue;

      ins.loop_start  = fget_u24le(fp) << 1;
      ins.loop_length = fget_u24le(fp) << 1;
      ins.volume      = fgetc(fp);
      ins.finetune    = fgetc(fp);

      if(ins.length == 0)
        continue;

      ins.packing = fgetc(fp);
      switch(ins.packing)
      {
        case SYM_instrument::UNCOMPRESSED_VIDC:
          m.uses[FT_SAMPLE_VIDC] = true;
          if(fseek(fp, ins.length, SEEK_CUR))
            return modutil::SEEK_ERROR;
          break;

        case SYM_instrument::LZW_DELTA_LINEAR:
          m.uses[FT_SAMPLE_LZW] = true;
          if(LZW_read(m.buffer, ins.length, ins.length, LZW_FLAGS_SYM, fp) != 0)
            return modutil::BAD_PACKING;
          break;

        case SYM_instrument::UNCOMPRESSED_LINEAR:
          m.uses[FT_SAMPLE_LINEAR] = true;
          if(fseek(fp, ins.length, SEEK_CUR))
            return modutil::SEEK_ERROR;
          break;

        case SYM_instrument::UNCOMPRESSED_LINEAR_16:
          m.uses[FT_SAMPLE_LINEAR_16] = true;
          if(fseek(fp, ins.length << 1, SEEK_CUR))
            return modutil::SEEK_ERROR;
          break;

        case SYM_instrument::SIGMA_DELTA_LINEAR:
          m.uses[FT_SAMPLE_SIGMA_DELTA_LINEAR] = true;
          if(SigmaDelta::read(m.buffer, ins.length, fp) != modutil::SUCCESS)
            return modutil::BAD_PACKING;
          break;

        case SYM_instrument::SIGMA_DELTA_VIDC:
          m.uses[FT_SAMPLE_SIGMA_DELTA_VIDC] = true;
          if(SigmaDelta::read(m.buffer, ins.length, fp) != modutil::SUCCESS)
            return modutil::BAD_PACKING;
          break;

        default:
          format::error("invalid sample %zu packing type %u", i, ins.packing);
          return modutil::INVALID;
      }
    }

    /**
     * Text.
     */
    if(h.text_length)
    {
      h.text_packing = fgetc(fp);
      if(feof(fp) || h.text_packing > 1)
      {
        format::error("invalid text packing %u", h.text_packing);
        return modutil::INVALID;
      }

      if(h.text_packing)
      {
        if(LZW_read(m.text, h.text_length, h.text_length, LZW_FLAGS_SYM, fp) != 0)
          return modutil::BAD_PACKING;
      }
      else
      {
        if(!fread(m.text, h.text_length, 1, fp))
          return modutil::READ_ERROR;
      }
      m.text[h.text_length] = '\0';
    }

    /**
     * Print information.
     */
    format::line("Name",     "%s", m.name);
    format::line("Type",     "Digital Symphony v%d", h.version);
    format::line("Instr.",   "%zu", m.num_samples);
    format::line("Channels", "%u", h.num_channels);
    format::line("Tracks",   "%u", h.num_tracks);
    format::line("Orders",   "%u", h.num_orders);
    format::uses(m.uses, FEATURE_STR);
    format::description("Text", m.text, h.text_length);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static constexpr const char *labels[] =
      {
        "Name", "Length", "LoopStart", "LoopLen", "Vol", "Fine", "Pack"
      };

      table::table<
        table::string<32>, // up to 64?
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<4>> s_table;

      format::line();
      s_table.header("Instr.", labels);

      for(size_t i = 0; i < MAX_SAMPLES; i++)
      {
        SYM_instrument &ins = m.instruments[i];
        if(ins.name_length & 0x80)
        {
          // Wiped instruments can sometimes still have name data...
          size_t num = ins.name_length & 0x3f;
          size_t j;
          for(j = 0; j < num; j++)
            if(ins.name[j] && ins.name[j] != ' ')
              break;
          if(j >= num)
            continue;
        }

        s_table.row(i + 1, ins.name, {},
          ins.length, ins.loop_start, ins.loop_length, {},
          ins.volume, ins.finetune, ins.packing);
      }
    }

    if(Config.dump_patterns)
    {
      if(!Config.dump_pattern_rows)
        format::line();

      struct effectSYM
      {
        uint8_t effect;
        uint16_t param;
        static constexpr int width() { return 6; }
        bool can_print() const { return effect > 0 || param > 0; }
        void print() const { if(can_print()) fprintf(stderr, " %2x%03x", effect, param); else format::spaces(width()); }
//        void print() const { if(can_print()) fprintf(stderr, HIGHLIGHT_FX("%2x%03x", effect, param), effect, param); else format::spaces(width()); }
      };

      for(size_t i = 0; i < h.num_orders; i++)
      {
        SYM_order &p = m.orders[i];

        using EVENT = format::event<format::note, format::sample, effectSYM>;
        format::pattern<EVENT> pattern(i, h.num_channels, NUM_ROWS);
        pattern.labels("Ord.", "Order");

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          pattern.tracks(p.tracks);
          continue;
        }

        for(size_t row = 0; row < NUM_ROWS; row++)
        {
          for(size_t track = 0; track < h.num_channels; track++)
          {
            int idx = p.tracks[track];
            SYM_track &t = idx < h.num_tracks ? m.tracks[idx] : m.blank_track;
            SYM_event &e = t.events[row];

            format::note   a{ e.note };
            format::sample b{ e.instrument };
            effectSYM      c{ e.effect, e.param };

            pattern.insert(EVENT(a, b, c));
          }
        }
        pattern.print(nullptr, p.tracks);
      }
    }

    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!num_syms)
      return;

    format::report("Total SYMs", num_syms);
  }
};

static const SYM_loader loader;
