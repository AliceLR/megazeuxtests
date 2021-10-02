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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

static int total_gdms = 0;


enum GDM_features
{
  FT_SURROUND,
  FT_CHANNEL_PAN,
  FT_SAMPLE_VOLUME,
  FT_NO_SAMPLE_VOLUME,
  FT_SAMPLE_PAN,
  FT_SAMPLE_COMPRESSION,
  FT_EVENT_NO_NOTE,
  FT_EVENT_NO_INST,
  FT_FX_UNKNOWN,
  FT_FX_PORTAMENTO_UP,
  FT_FX_PORTAMENTO_DOWN,
  FT_FX_TONEPORTA,
  FT_FX_VIBRATO,
  FT_FX_VOLSLIDE_TONEPORTA,
  FT_FX_VOLSLIDE_VIBRATO,
  FT_FX_TREMOLO,
  FT_FX_TREMOR,
  FT_FX_OFFSET,
  FT_FX_VOLSLIDE,
  FT_FX_JUMP,
  FT_FX_VOLUME,
  FT_FX_BREAK,
  FT_FX_FILTER,
  FT_FX_PORTAMENTO_FINE,
  FT_FX_GLISSANDO,
  FT_FX_VIBRATO_WAVEFORM,
  FT_FX_C4_TUNING,
  FT_FX_LOOP,
  FT_FX_TREMOLO_WAVEFORM,
  FT_FX_PORTAMENTO_EXTRA_FINE,
  FT_FX_VOLSLIDE_FINE,
  FT_FX_NOTE_CUT,
  FT_FX_NOTE_DELAY,
  FT_FX_PATTERN_DELAY,
  FT_FX_FUNKREPEAT,
  FT_FX_TEMPO,
  FT_FX_ARPEGGIO,
  FT_FX_SETFLAG,
  FT_FX_RETRIGGER,
  FT_FX_GLOBAL_VOLUME,
  FT_FX_VIBRATO_FINE,
  FT_FX_SAMPLE_CTRL,
  FT_FX_PAN,
  FT_FX_FREQ,
  FT_FX_SPECIAL_UNKNOWN,
  FT_FX_BPM,
  FT_FX_CH3,
  FT_FX_CH4,
  FT_OVER_64_ROWS,
  FT_OVER_256_ROWS,
  NUM_FEATURES
};


static const char *FEATURE_STR[NUM_FEATURES] =
{
  "Surround",
  "ChPan",
  "SVol",
  "NoSVol",
  "SPan",
  "SCmpr",
  "NoNote",
  "NoInst",
  "FXUnknown",
  "FXPortaUp",
  "FXPortaDn",
  "FXToneporta",
  "FXVibrato",
  "FXVolPorta",
  "FXVolVibr",
  "FXTremolo",
  "FXTremor",
  "FXOffset",
  "FXVolslide",
  "FXJump",
  "FXVolume",
  "FXBreak",
  "FXFilter",
  "FXPortaFine",
  "FXGliss",
  "FXVibrWF",
  "FXFinetune",
  "FXLoop",
  "FXTremWF",
  "FXPortaExFine",
  "FXVolFine",
  "FXNoteCut",
  "FXNoteDelay",
  "FXPattDelay",
  "FXInvLoop",
  "FXTempo",
  "FXArpeggio",
  "FXSetFlag",
  "FXRetrig",
  "FXGVol",
  "FXVibrFine",
  "FXSmplCtrl",
  "FXPan",
  "FXFreq",
  "FXUnknownSp",
  "FXBPM",
  "FXCh3",
  "FXCh4",
  ">64Rows",
  ">256Rows",
};

static const char MAGIC[] = "GDM\xFE";
static const char MAGIC_EOF[] = "\x0D\x0A\x1A";
static const char MAGIC_2[] = "GMFS";

static const char *TRACKERS[] =
{
  "2GDM"
};
static const int NUM_TRACKERS = arraysize(TRACKERS);

static const char *FORMATS[] =
{
  ".GDM",
  ".MOD",
  ".MTM",
  ".S3M",
  ".669",
  ".FAR",
  ".ULT",
  ".STM",
  ".MED"
};
static const int NUM_FORMATS = arraysize(FORMATS);

static constexpr const char *TRACKER(uint16_t tracker)
{
  return (tracker < NUM_TRACKERS) ? TRACKERS[tracker] : "unknown";
}

static constexpr const char *FORMAT(uint16_t format)
{
  return (format < NUM_FORMATS) ? FORMATS[format] : "unknown";
}

static constexpr uint8_t VER_MINOR(uint16_t version)
{
  return version >> 8;
}

static constexpr uint8_t VER_MAJOR(uint16_t version)
{
  return version & 0xFF;
}

enum GDM_sample_flags
{
  S_LOOP   = (1<<0),
  S_S16    = (1<<1),
  S_VOL    = (1<<2),
  S_PAN    = (1<<3),
  S_LZW    = (1<<4),
  S_STEREO = (1<<5),
};

enum GDM_effects
{
  E_NONE,
  E_PORTAMENTO_UP,
  E_PORTAMENTO_DOWN,
  E_TONEPORTA,
  E_VIBRATO,
  E_VOLSLIDE_TONEPORTA,
  E_VOLSLIDE_VIBRATO,
  E_TREMOLO,
  E_TREMOR,
  E_SAMPLE_OFFSET,
  E_VOLSLIDE,
  E_PATTERN_JUMP,
  E_VOLUME,
  E_PATTERN_BREAK,
  E_EXT,
  E_TEMPO,
  E_ARPEGGIO,
  E_SETFLAG,
  E_RETRIGGER,
  E_GLOBAL_VOLUME,
  E_VIBRATO_FINE,
  E_SPECIAL            = 0x1e,
  E_BPM                = 0x1f,
};

enum GDM_effects_ext
{
  EX_FILTER,
  EX_FINE_PORTAMENTO_UP,
  EX_FINE_PORTAMENTO_DOWN,
  EX_GLISSANDO,
  EX_VIBRATO_WAVEFORM,
  EX_C4_TUNING,
  EX_LOOP,
  EX_TREMOLO_WAVEFORM,
  EX_EXTRA_FINE_PORTAMENTO_UP,
  EX_EXTRA_FINE_PORTAMENTO_DOWN,
  EX_FINE_VOLSLIDE_UP,
  EX_FINE_VOLSLIDE_DOWN,
  EX_NOTE_CUT,
  EX_NOTE_DELAY,
  EX_EXTEND_ROW,
  EX_FUNKREPEAT
};

enum GDM_effects_special
{
  ES_SAMPLE_CTRL = 0x0,
  ES_PAN         = 0x8,
  ES_FREQ        = 0xd,
};

template<int N>
static const char *FLAG_STR(char (&buffer)[N], uint8_t flags)
{
  static_assert(N >= 7, "use a bigger buffer pls ;-(");
  buffer[0] = (flags & S_LOOP)   ? 'r' : ' ';
  buffer[1] = (flags & S_S16)    ? 'w' : ' ';
  buffer[2] = (flags & S_VOL)    ? 'v' : ' ';
  buffer[3] = (flags & S_PAN)    ? 'p' : ' ';
  buffer[4] = (flags & S_LZW)    ? 'x' : ' ';
  buffer[5] = (flags & S_STEREO) ? 's' : ' ';
  buffer[6] = '\0';
  return buffer;
}

struct GDM_header
{
  char magic[4];
  char name[33]; // Null-terminator not stored? array size is 1 larger than field.
  char author[33]; // Ditto.
  char eof[3];
  char magic2[4];
  uint16_t gdm_version;
  uint16_t tracker_id;
  uint16_t tracker_version;
  uint8_t panning[32];
  uint8_t global_volume;
  uint8_t tempo;
  uint8_t bpm;
  uint16_t original_format;
  uint32_t order_offset;
  uint8_t num_orders;
  uint32_t pattern_offset;
  uint8_t num_patterns;
  uint32_t sample_offset;
  uint32_t sample_data_offset;
  uint8_t num_samples;
  uint32_t message_offset;
  uint32_t message_length;
  uint32_t scrolly_offset; // ??
  uint16_t scrolly_length;
  uint32_t graphic_offset; // ??
  uint16_t graphic_length;
};

struct GDM_sample
{
  char name[33]; // Null-terminator not stored? Array size is 1 larger than field.
  char filename[13]; // Ditto.
  uint8_t ems; // ignore.
  uint32_t length;
  uint32_t loopstart;
  uint32_t loopend;
  uint8_t flags;
  uint16_t c4rate;
  uint8_t default_volume;
  uint8_t default_panning;
};

struct GDM_note
{
  uint8_t note;
  uint8_t sample;
  struct
  {
    uint8_t effect;
    uint8_t param;
  } effects[4];
};

struct GDM_pattern
{
  // NOTE: this could theoretically be longer but this is probably the maximum.
  GDM_note rows[256][32];
  uint8_t max_track_effects[32];
  uint16_t raw_size;
  uint16_t num_rows;
};

struct GDM_data
{
  GDM_header header;
  GDM_sample samples[256];
  GDM_pattern *patterns[256];
  uint8_t orders[256];
  uint8_t buffer[65536];
  uint8_t num_channels;
  char *message = nullptr;

  bool uses[NUM_FEATURES];

  ~GDM_data()
  {
    delete[] message;
    for(int i = 0; i < 256; i++)
      delete patterns[i];
  }
};

static enum GDM_features get_effect_feature(uint8_t fx_effect, uint8_t fx_param)
{
  switch(fx_effect)
  {
    default:                   return FT_FX_UNKNOWN;
    case E_PORTAMENTO_UP:      return FT_FX_PORTAMENTO_UP;
    case E_PORTAMENTO_DOWN:    return FT_FX_PORTAMENTO_DOWN;
    case E_TONEPORTA:          return FT_FX_TONEPORTA;
    case E_VIBRATO:            return FT_FX_VIBRATO;
    case E_VOLSLIDE_TONEPORTA: return FT_FX_VOLSLIDE_TONEPORTA;
    case E_VOLSLIDE_VIBRATO:   return FT_FX_VOLSLIDE_VIBRATO;
    case E_TREMOLO:            return FT_FX_TREMOLO;
    case E_TREMOR:             return FT_FX_TREMOR;
    case E_SAMPLE_OFFSET:      return FT_FX_OFFSET;
    case E_VOLSLIDE:           return FT_FX_VOLSLIDE;
    case E_PATTERN_JUMP:       return FT_FX_JUMP;
    case E_VOLUME:             return FT_FX_VOLUME;
    case E_PATTERN_BREAK:      return FT_FX_BREAK;
    case E_TEMPO:              return FT_FX_TEMPO;
    case E_ARPEGGIO:           return FT_FX_ARPEGGIO;
    case E_SETFLAG:            return FT_FX_SETFLAG;
    case E_RETRIGGER:          return FT_FX_RETRIGGER;
    case E_GLOBAL_VOLUME:      return FT_FX_GLOBAL_VOLUME;
    case E_VIBRATO_FINE:       return FT_FX_VIBRATO_FINE;
    case E_BPM:                return FT_FX_BPM;
    case E_EXT:
    {
      uint8_t fx_ext = (fx_param >> 4) & 0x0F;
      switch(fx_ext)
      {
        case EX_FILTER:                     return FT_FX_FILTER;
        case EX_FINE_PORTAMENTO_UP:         return FT_FX_PORTAMENTO_FINE;
        case EX_FINE_PORTAMENTO_DOWN:       return FT_FX_PORTAMENTO_FINE;
        case EX_GLISSANDO:                  return FT_FX_GLISSANDO;
        case EX_VIBRATO_WAVEFORM:           return FT_FX_VIBRATO_WAVEFORM;
        case EX_C4_TUNING:                  return FT_FX_C4_TUNING;
        case EX_LOOP:                       return FT_FX_LOOP;
        case EX_TREMOLO_WAVEFORM:           return FT_FX_TREMOLO_WAVEFORM;
        case EX_EXTRA_FINE_PORTAMENTO_UP:   return FT_FX_PORTAMENTO_EXTRA_FINE;
        case EX_EXTRA_FINE_PORTAMENTO_DOWN: return FT_FX_PORTAMENTO_EXTRA_FINE;
        case EX_FINE_VOLSLIDE_UP:           return FT_FX_VOLSLIDE_FINE;
        case EX_FINE_VOLSLIDE_DOWN:         return FT_FX_VOLSLIDE_FINE;
        case EX_NOTE_CUT:                   return FT_FX_NOTE_CUT;
        case EX_NOTE_DELAY:                 return FT_FX_NOTE_DELAY;
        case EX_EXTEND_ROW:                 return FT_FX_PATTERN_DELAY;
        case EX_FUNKREPEAT:                 return FT_FX_FUNKREPEAT;
        default: /* shouldn't happen. */    return FT_FX_UNKNOWN;
      }
    }
    case E_SPECIAL:
    {
      uint8_t fx_special = (fx_param >> 4) & 0x0F;
      switch(fx_special)
      {
        case ES_SAMPLE_CTRL: return FT_FX_SAMPLE_CTRL;
        case ES_PAN:         return FT_FX_PAN;
        case ES_FREQ:        return FT_FX_FREQ;
        default:             return FT_FX_SPECIAL_UNKNOWN;
       }
    }
  }
}

static modutil::error GDM_read(FILE *fp)
{
  GDM_data m{};
  GDM_header &h = m.header;

  if(!fread(h.magic, 4, 1, fp) ||
     !fread(h.name, 32, 1, fp) ||
     !fread(h.author, 32, 1, fp) ||
     !fread(h.eof, 3, 1, fp) ||
     !fread(h.magic2, 4, 1, fp))
    return modutil::READ_ERROR;

  if(memcmp(h.magic, MAGIC, 4) ||
     memcmp(h.eof, MAGIC_EOF, 3) ||
     memcmp(h.magic2, MAGIC_2, 4))
    return modutil::FORMAT_ERROR;

  total_gdms++;

  h.name[32] = '\0';
  h.author[32] = '\0';

  h.gdm_version         = fget_u16le(fp);
  h.tracker_id          = fget_u16le(fp);
  h.tracker_version     = fget_u16le(fp);

  if(!fread(h.panning, 32, 1, fp))
    return modutil::READ_ERROR;

  h.global_volume       = fgetc(fp);
  h.tempo               = fgetc(fp);
  h.bpm                 = fgetc(fp);
  h.original_format     = fget_u16le(fp);
  h.order_offset        = fget_u32le(fp);
  h.num_orders          = fgetc(fp) + 1;
  h.pattern_offset      = fget_u32le(fp);
  h.num_patterns        = fgetc(fp) + 1;
  h.sample_offset       = fget_u32le(fp);
  h.sample_data_offset  = fget_u32le(fp);
  h.num_samples         = fgetc(fp) + 1;
  h.message_offset      = fget_u32le(fp);
  h.message_length      = fget_u32le(fp);
  h.scrolly_offset      = fget_u32le(fp);
  h.scrolly_length      = fget_u16le(fp);
  h.graphic_offset      = fget_u32le(fp);
  h.graphic_length      = fget_u16le(fp);

  if(feof(fp))
    return modutil::READ_ERROR;

  // Get channel count by checking for 255 in the panning table.
  for(int i = 0; i < 32; i++)
  {
    if(h.panning[i] != 255)
    {
      m.num_channels = i + 1;

      if(h.panning[i] == 16)
        m.uses[FT_SURROUND] = true;
      if(h.panning[i] != 8)
        m.uses[FT_CHANNEL_PAN] = true;
    }
  }

  // Order list.
  if(fseek(fp, h.order_offset, SEEK_SET))
    return modutil::SEEK_ERROR;

  if(!fread(m.orders, h.num_orders, 1, fp))
    return modutil::READ_ERROR;

  // Samples.
  if(fseek(fp, h.sample_offset, SEEK_SET))
    return modutil::SEEK_ERROR;

  for(size_t i = 0; i < h.num_samples; i++)
  {
    GDM_sample &s = m.samples[i];
    if(!fread(s.name, 32, 1, fp) || !fread(s.filename, 12, 1, fp))
      return modutil::READ_ERROR;

    s.name[32] = '\0';
    s.filename[12] = '\0';

    s.ems             = fgetc(fp); // Safe to ignore.
    s.length          = fget_u32le(fp);
    s.loopstart       = fget_u32le(fp);
    s.loopend         = fget_u32le(fp);
    s.flags           = fgetc(fp);
    s.c4rate          = fget_u16le(fp);
    s.default_volume  = fgetc(fp);
    s.default_panning = fgetc(fp);

    if(feof(fp))
      return modutil::READ_ERROR;

    if((s.flags & S_VOL) && s.default_volume != 255)
      m.uses[FT_SAMPLE_VOLUME] = true;
    else
      m.uses[FT_NO_SAMPLE_VOLUME] = true;
    if((s.flags & S_PAN) && s.default_panning != 255)
    {
      if(s.default_panning == 16)
        m.uses[FT_SURROUND] = true;

      m.uses[FT_SAMPLE_PAN] = true;
    }
    if(s.flags & S_LZW)
      m.uses[FT_SAMPLE_COMPRESSION] = true;
  }

  // Patterns.
  if(fseek(fp, h.pattern_offset, SEEK_SET))
    return modutil::SEEK_ERROR;

  for(size_t i = 0; i < h.num_patterns; i++)
  {
    GDM_pattern *p = new GDM_pattern{};
    m.patterns[i] = p;
    if(!p)
      return modutil::ALLOC_ERROR;

    p->raw_size = fget_u16le(fp) - 2;

    size_t pos = 0;
    size_t row = 0;
    while(pos < p->raw_size && row < (size_t)arraysize(p->rows))
    {
      uint8_t t = fgetc(fp);
      pos++;

      if(t == 0)
      {
        row++;
        if(pos < p->raw_size)
          continue;

        break;
      }

      uint8_t track = (t & 0x1F);
      GDM_note &n = p->rows[row][track];

      if(t & 0x20)
      {
        uint8_t note = fgetc(fp);
        uint8_t inst = fgetc(fp);
        pos += 2;

        n.note = note;
        n.sample = inst;

        if(!note)
          m.uses[FT_EVENT_NO_NOTE] = true;
        if(!inst)
          m.uses[FT_EVENT_NO_INST] = true;
      }

      if(t & 0x40)
      {
        int j;
        for(j = 0; j < 3; j++)
        {
          uint8_t fx = fgetc(fp);
          uint8_t fx_param = fgetc(fp);
          pos += 2;

          uint8_t fx_effect = fx & 0x1F;
          uint8_t fx_channel = (fx >> 6) & 0x03;

          if(fx_channel + 1 > p->max_track_effects[track])
            p->max_track_effects[track] = fx_channel + 1;

          n.effects[fx_channel].effect = fx_effect;
          n.effects[fx_channel].param = fx_param;

          if(fx_channel == 0x02)
            m.uses[FT_FX_CH3] = true;
          if(fx_channel == 0x03)
            m.uses[FT_FX_CH4] = true;

          if(fx_effect)
          {
            enum GDM_features fx_feature = get_effect_feature(fx_effect, fx_param);
            if(fx_feature == FT_FX_UNKNOWN)
              format::warning("unknown effect: %02x %02x", fx_effect, fx_param);
            m.uses[fx_feature] = true;
          }

          if(!(fx & 0x20))
            break;
        }
        if(j >= 4)
          return modutil::GDM_TOO_MANY_EFFECTS;
      }
    }
    if(feof(fp))
      return modutil::READ_ERROR;

    if(pos != p->raw_size)
      return modutil::GDM_BAD_PATTERN;

    p->num_rows = row;
    if(row > 64)
      m.uses[FT_OVER_64_ROWS] = true;
    if(row > 256)
      m.uses[FT_OVER_256_ROWS] = true;
  }

  // Message.
  if(h.message_offset && h.message_length)
  {
    if(!fseek(fp, h.message_offset, SEEK_SET))
    {
      m.message = new char[h.message_length];
      size_t end = fread(m.message, h.message_length, 1, fp);
      m.message[end] = MIN((size_t)h.message_length, end);
    }
  }

  /* Print metadata. */
  format::line("Name",     "%s", h.name);
  format::line("Type",     "GDM %u.%u (%s/%s %u.%u)",
   VER_MAJOR(h.gdm_version), VER_MINOR(h.gdm_version), FORMAT(h.original_format),
   TRACKER(h.tracker_id), VER_MAJOR(h.tracker_version), VER_MINOR(h.tracker_version));
  format::line("Samples",  "%u", h.num_samples);
  format::line("Channels", "%u", m.num_channels);
  format::line("Patterns", "%u", h.num_patterns);
  format::line("Orders",   "%u", h.num_orders);
  format::uses(m.uses, FEATURE_STR);
  format::description("Desc.", m.message, h.message_length);

  /* Print samples. */
  if(Config.dump_samples)
  {
    char tmp[16];

    format::line();

    static const char *labels[] =
    {
      "Name", "Filename", "Length", "LoopStart", "LoopEnd", "Flags", "C4Rate", "Vol", "Pan"
    };

    namespace table = format::table;
    table::table<
      table::string<32>,
      table::string<12>,
      table::spacer,
      table::number<10, table::LEFT>,
      table::number<10, table::LEFT>,
      table::number<10, table::LEFT>,
      table::string<7>,
      table::number<7>,
      table::number<4>,
      table::number<4>> s_table;

    s_table.header("Samples", labels);

    for(unsigned int i = 0; i < h.num_samples; i++)
    {
      GDM_sample &s = m.samples[i];
      s_table.row(i, s.name, s.filename, {}, s.length, s.loopstart, s.loopend,
        FLAG_STR(tmp, s.flags), s.c4rate, s.default_volume, s.default_panning);
    }
  }

  if(Config.dump_patterns)
  {
    format::line();

    O_("Panning :");
    for(unsigned int k = 0; k < m.num_channels; k++)
    {
      if(h.panning[k] == 255)
        continue;
      fprintf(stderr, " %02x", h.panning[k]);
    }
    fprintf(stderr, "\n");

    format::orders("Orders", m.orders, h.num_orders);

    for(unsigned int i = 0; i < h.num_patterns; i++)
    {
      GDM_pattern *p = m.patterns[i];

      using EVENT = format::event<format::note, format::sample, format::effectWide,
                                  format::effectWide, format::effectWide, format::effectWide>;
      format::pattern<EVENT> pattern(i, m.num_channels, p->num_rows, p->raw_size);

      if(!Config.dump_pattern_rows)
      {
        pattern.summary();
        continue;
      }

      for(unsigned int row = 0; row < p->num_rows; row++)
      {
        for(unsigned int track = 0; track < m.num_channels; track++)
        {
          if(h.panning[track] == 255)
          {
            pattern.skip();
            continue;
          }

          GDM_note &n = p->rows[row][track];
          format::note       a{ n.note };
          format::sample     b{ n.sample };
          format::effectWide c{ n.effects[0].effect, n.effects[0].param };
          format::effectWide d{ n.effects[1].effect, n.effects[1].param };
          format::effectWide e{ n.effects[2].effect, n.effects[2].param };
          format::effectWide f{ n.effects[3].effect, n.effects[3].param };

          pattern.insert(EVENT(a, b, c, d, e, f));
        }
      }
      pattern.print();
    }
  }
  return modutil::SUCCESS;
}

class GDM_loader : modutil::loader
{
public:
  GDM_loader(): modutil::loader("GDM : General Digital Music") {}

  virtual modutil::error load(FILE *fp) const override
  {
    return GDM_read(fp);
  }

  virtual void report() const override
  {
    if(!total_gdms)
      return;

    format::report("Total GDMs", total_gdms);
  }
};

static const GDM_loader loader;
