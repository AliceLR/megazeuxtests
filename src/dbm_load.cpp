/**
 * Copyright (C) 2020-2024 Lachesis <petrifiedrowan@gmail.com>
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

#include "IFF.hpp"
#include "modutil.hpp"

static int total_dbm = 0;


enum DBM_features
{
  FT_MULTIPLE_SONGS,
  FT_ROWS_OVER_64,
  FT_ROWS_OVER_256,
  FT_CHUNK_ORDER,
  FT_CHUNK_OVER_4_MIB,
  FT_VENV_CHUNK,
  FT_PENV_CHUNK,
  FT_DSPE_CHUNK,
  FT_BAD_VOLUME_ENVELOPE,
  FT_BAD_PAN_ENVELOPE,
  FT_NEGATIVE_ENVELOPE_VALUE,
  FT_HIGH_ENVELOPE_VALUE,
  FT_S_8_BIT,
  FT_S_16_BIT,
  FT_S_32_BIT,
  FT_S_UNKNOWN_FORMAT,
  FT_E_ARPEGGIO,
  FT_E_PORTAMENTO,
  FT_E_TONEPORTA,
  FT_E_VIBRATO,
  FT_E_TONEPORTA_VOLSLIDE,
  FT_E_VIBRATO_VOLSLIDE,
  FT_E_PANNING,
  FT_E_OFFSET,
  FT_E_VOLSLIDE,
  FT_E_JUMP,
  FT_E_VOLUME,
  FT_E_BREAK,
  FT_E_FILTER,
  FT_E_FINE_PORTAMENTO,
  FT_E_REVERSE,
  FT_E_TURN_OFF_SOUND,
  FT_E_TURN_OFF_CHANNEL,
  FT_E_LOOP,
  FT_E_OFFSET_2,
  FT_E_PANNING_2,
  FT_E_RETRIG,
  FT_E_FINE_VOLSLIDE,
  FT_E_NOTE_CUT,
  FT_E_NOTE_DELAY,
  FT_E_PATTERN_DELAY,
  FT_E_TEMPO_BPM,
  FT_E_GLOBAL_VOLUME,
  FT_E_GLOBAL_VOLSLIDE,
  FT_E_KEY_OFF,
  FT_E_ENVELOPE_POSITION,
  FT_E_OFFSET_SLIDE,
  FT_E_PANNING_SLIDE,
  FT_E_BPM,
  FT_E_ECHO,
  FT_E_ECHO_DELAY,
  FT_E_ECHO_FEEDBACK,
  FT_E_ECHO_MIX,
  FT_E_ECHO_CROSS,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  ">1Song",
  ">64Rows",
  ">256Rows",
  "Misordered",
  ">4MBChunk",
  "VENV",
  "PENV",
  "DSPE",
  "BadVolEnv",
  "BadPanEnv",
  "EnvPt<0",
  "EnvPt>64",
  "S:8",
  "S:16",
  "S:32",
  "S:???",
  "E:Arpeggio",
  "E:Porta",
  "E:Toneporta",
  "E:Vibrato",
  "E:TPorta+Vol",
  "E:Vib+Vol",
  "E:Pan",
  "E:Offset",
  "E:Volslide",
  "E:Jump",
  "E:Volume",
  "E:Break",
  "E:Filter",
  "E:FinePorta",
  "E:Reverse",
  "E:TurnOffSnd",
  "E:TurnOffChn",
  "E:Loop",
  "E:E7Offset",
  "E:E8Pan",
  "E:Retrig",
  "E:FineVol",
  "E:NoteCut",
  "E:NoteDelay",
  "E:PatDelay",
  "E:TempoBPM",
  "E:GVolume",
  "E:GVolslide",
  "E:KeyOff",
  "E:EnvPos",
  "E:OffsetSlide",
  "E:PanSlide",
  "E:BPM",
  "E:Echo",
  "E:EchoDelay",
  "E:EchoFeedback",
  "E:EchoMix",
  "E:EchoCross",
};

static const int MAX_SONGS = 16;
static const int MAX_INSTRUMENTS = 256;
static const int MAX_SAMPLES = 256;
static const int MAX_PATTERNS = 256;

enum DBM_effects
{
  E_ARPEGGIO            = 0x00,
  E_PORTAMENTO_UP       = 0x01,
  E_PORTAMENTO_DN       = 0x02,
  E_TONEPORTA           = 0x03,
  E_VIBRATO             = 0x04,
  E_TONEPORTA_VOLSLIDE  = 0x05,
  E_VIBRATO_VOLSLIDE    = 0x06,
  E_PANNING             = 0x08,
  E_OFFSET              = 0x09,
  E_VOLSLIDE            = 0x0a,
  E_JUMP                = 0x0b,
  E_VOLUME              = 0x0c,
  E_BREAK               = 0x0d,
  E_EXTENDED            = 0x0e,
  E_TEMPO_BPM           = 0x0f,
  E_GLOBAL_VOLUME       = 0x10,
  E_GLOBAL_VOLSLIDE     = 0x11,
  E_KEY_OFF             = 0x14,
  E_ENVELOPE_POSITION   = 0x15,
  E_OFFSET_SLIDE        = 0x18,
  E_PANNING_SLIDE       = 0x19,
  E_BPM                 = 0x1c,
  E_ECHO                = 0x1f,
  E_ECHO_DELAY          = 0x20,
  E_ECHO_FEEDBACK       = 0x21,
  E_ECHO_MIX            = 0x22,
  E_ECHO_CROSS          = 0x23,

  EX_FILTER             = 0x0,
  EX_FINE_PORTAMENTO_UP = 0x1,
  EX_FINE_PORTAMENTO_DN = 0x2,
  EX_REVERSE            = 0x3,
  EX_TURN_OFF_SOUND     = 0x4,
  EX_TURN_OFF_CHANNEL   = 0x5,
  EX_LOOP               = 0x6,
  EX_OFFSET             = 0x7,
  EX_PANNING            = 0x8,
  EX_RETRIG             = 0x9,
  EX_FINE_VOLUME_UP     = 0xa,
  EX_FINE_VOLUME_DN     = 0xb,
  EX_NOTE_CUT           = 0xc,
  EX_NOTE_DELAY         = 0xd,
  EX_PATTERN_DELAY      = 0xe,
};

struct DBM_song
{
  char name[45];
  uint16_t num_orders;
  uint16_t *orders = nullptr;

  ~DBM_song()
  {
    delete[] orders;
  }
};

struct DBM_instrument
{
  enum flags
  {
    FORWARD_LOOP = (1 << 0),
    BIDI_LOOP    = (1 << 1),
  };

  char name[31];
  uint16_t sample_id;
  uint16_t volume;
  uint32_t finetune_hz;
  uint32_t repeat_start; /* in samples(?) */
  uint32_t repeat_length; /* in samples(?) */
  int16_t panning;
  uint16_t flags;
};

struct DBM_sample
{
  enum flags
  {
    S_8_BIT  = (1 << 0),
    S_16_BIT = (1 << 1),
    S_32_BIT = (1 << 2),
  };

  const char *type_str() const
  {
    if(flags & S_8_BIT)
      return "8-bit";
    if(flags & S_16_BIT)
      return "16-bit";
    if(flags & S_32_BIT)
      return "32-bit";
    return "?";
  }

  uint32_t flags;
  uint32_t length; /* in samples. */
};

struct DBM_pattern
{
  enum flags
  {
    NOTE       = (1 << 0),
    INSTRUMENT = (1 << 1),
    EFFECT_1   = (1 << 2),
    PARAM_1    = (1 << 3),
    EFFECT_2   = (1 << 4),
    PARAM_2    = (1 << 5)
  };

  struct note
  {
    uint8_t note;
    uint8_t instrument;
    uint8_t effect_1;
    uint8_t param_1;
    uint8_t effect_2;
    uint8_t param_2;
  };

  uint16_t num_rows;
  uint32_t packed_data_size;
  note     *data;
  char     *name; /* From PNAM. */

  ~DBM_pattern()
  {
    delete[] data;
    delete[] name;
  }
};

struct DBM_envelope
{
  static const size_t MAX_POINTS = 32;

  enum flags
  {
    ENABLED   = (1 << 0),
    SUSTAIN_1 = (1 << 1),
    LOOP      = (1 << 2),
    SUSTAIN_2 = (1 << 3),
  };

  struct point
  {
    uint16_t time;
    int16_t value;
  };

  uint16_t instrument_id;
  uint8_t flags;
  uint8_t num_points;
  uint8_t sustain_1_point;
  uint8_t loop_start_point;
  uint8_t loop_end_point;
  uint8_t sustain_2_point;
  DBM_envelope::point points[32];
};

struct DBM_data
{
  /* Header (8) */

  char magic[4];
  uint16_t tracker_version;
  uint16_t reserved;

  /* NAME (44) */

  char name[45];
  char name_stripped[45];
  bool read_name = false;

  /* INFO (10) */

  uint16_t num_instruments;
  uint16_t num_samples;
  uint16_t num_songs;
  uint16_t num_patterns;
  uint16_t num_channels;
  bool read_info = false;

  /* SONG */

  DBM_song songs[MAX_SONGS];

  /* PATT and PNAM */

  DBM_pattern patterns[MAX_PATTERNS];
  uint16_t pattern_name_encoding;
  bool pattern_names;

  /* INST */

  DBM_instrument instruments[MAX_INSTRUMENTS];

  /* SMPL */

  DBM_sample samples[MAX_SAMPLES];

  /* VENV */

  uint16_t num_volume_envelopes;
  DBM_envelope *volume_envelopes;

  /* PENV */

  uint16_t num_pan_envelopes;
  DBM_envelope *pan_envelopes;

  /* DSPE */

  uint16_t dspe_mask_length;
  uint8_t *dspe_mask;
  uint16_t dspe_global_echo_delay;
  uint16_t dspe_global_echo_feedback;
  uint16_t dspe_global_echo_mix;
  uint16_t dspe_cross_channel_echo;

  bool uses[NUM_FEATURES];

  ~DBM_data()
  {
    delete[] volume_envelopes;
    delete[] pan_envelopes;
    delete[] dspe_mask;
  }
};


static DBM_features effect_feature(uint8_t effect, uint8_t param)
{
  switch(effect)
  {
    case E_ARPEGGIO:           return FT_E_ARPEGGIO;
    case E_PORTAMENTO_UP:      return FT_E_PORTAMENTO;
    case E_PORTAMENTO_DN:      return FT_E_PORTAMENTO;
    case E_TONEPORTA:          return FT_E_TONEPORTA;
    case E_VIBRATO:            return FT_E_VIBRATO;
    case E_TONEPORTA_VOLSLIDE: return FT_E_TONEPORTA_VOLSLIDE;
    case E_VIBRATO_VOLSLIDE:   return FT_E_VIBRATO_VOLSLIDE;
    case E_PANNING:            return FT_E_PANNING;
    case E_OFFSET:             return FT_E_OFFSET;
    case E_VOLSLIDE:           return FT_E_VOLSLIDE;
    case E_JUMP:               return FT_E_JUMP;
    case E_VOLUME:             return FT_E_VOLUME;
    case E_BREAK:              return FT_E_BREAK;
    case E_TEMPO_BPM:          return FT_E_TEMPO_BPM;
    case E_GLOBAL_VOLUME:      return FT_E_GLOBAL_VOLUME;
    case E_GLOBAL_VOLSLIDE:    return FT_E_GLOBAL_VOLSLIDE;
    case E_KEY_OFF:            return FT_E_KEY_OFF;
    case E_ENVELOPE_POSITION:  return FT_E_ENVELOPE_POSITION;
    case E_OFFSET_SLIDE:       return FT_E_OFFSET_SLIDE;
    case E_PANNING_SLIDE:      return FT_E_PANNING_SLIDE;
    case E_BPM:                return FT_E_BPM;
    case E_ECHO:               return FT_E_ECHO;
    case E_ECHO_DELAY:         return FT_E_ECHO_DELAY;
    case E_ECHO_FEEDBACK:      return FT_E_ECHO_FEEDBACK;
    case E_ECHO_MIX:           return FT_E_ECHO_MIX;
    case E_ECHO_CROSS:         return FT_E_ECHO_CROSS;

    case E_EXTENDED:
      switch(param >> 4)
      {
        case EX_FILTER:             return FT_E_FILTER;
        case EX_FINE_PORTAMENTO_UP: return FT_E_FINE_PORTAMENTO;
        case EX_FINE_PORTAMENTO_DN: return FT_E_FINE_PORTAMENTO;
        case EX_REVERSE:            return FT_E_REVERSE;
        case EX_TURN_OFF_SOUND:     return FT_E_TURN_OFF_SOUND;
        case EX_TURN_OFF_CHANNEL:   return FT_E_TURN_OFF_CHANNEL;
        case EX_LOOP:               return FT_E_LOOP;
        case EX_OFFSET:             return FT_E_OFFSET_2;
        case EX_PANNING:            return FT_E_PANNING_2;
        case EX_RETRIG:             return FT_E_RETRIG;
        case EX_FINE_VOLUME_UP:     return FT_E_FINE_VOLSLIDE;
        case EX_FINE_VOLUME_DN:     return FT_E_FINE_VOLSLIDE;
        case EX_NOTE_CUT:           return FT_E_NOTE_CUT;
        case EX_NOTE_DELAY:         return FT_E_NOTE_DELAY;
        case EX_PATTERN_DELAY:      return FT_E_PATTERN_DELAY;
      }
      break;
  }
  return NUM_FEATURES;
}

static void check_event(DBM_data &m, const DBM_pattern::note &e)
{
  DBM_features a = effect_feature(e.effect_1, e.param_1);
  DBM_features b = effect_feature(e.effect_2, e.param_2);
  if(a != NUM_FEATURES)
    m.uses[a] = true;
  if(b != NUM_FEATURES)
    m.uses[b] = true;
}


class NAME_handler
{
public:
  static constexpr IFFCode id = IFFCode("NAME");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    if(len < 44)
    {
      format::error("NAME chunk length %zu, expected >=44.", len);
      return modutil::INVALID;
    }
    if(m.read_name)
    {
      format::error("duplicate NAME.");
      return modutil::INVALID;
    }

    if(!fread(m.name, 44, 1, fp))
      return modutil::READ_ERROR;

    m.name[44] = '\0';
    m.read_name = true;

    memcpy(m.name_stripped, m.name, arraysize(m.name));
    strip_module_name(m.name_stripped, arraysize(m.name_stripped));

    return modutil::SUCCESS;
  }
};

class INFO_handler
{
public:
  static constexpr IFFCode id = IFFCode("INFO");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    if(len < 10)
    {
      format::error("INFO chunk length %zu, expected >=10.", len);
      return modutil::INVALID;
    }
    if(m.read_info)
    {
      format::error("duplicate INFO.");
      return modutil::INVALID;
    }
    m.read_info = true;

    m.num_instruments = fget_u16be(fp);
    m.num_samples     = fget_u16be(fp);
    m.num_songs       = fget_u16be(fp);
    m.num_patterns    = fget_u16be(fp);
    m.num_channels    = fget_u16be(fp);

    if(feof(fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
};

class SONG_handler
{
public:
  static constexpr IFFCode id = IFFCode("SONG");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    if(len < 46 * m.num_songs)
    {
      format::error("SONG chunk length < %u", 46 * m.num_songs);
      return modutil::INVALID;
    }

    for(size_t i = 0; i < m.num_songs; i++)
    {
      if(i >= MAX_SONGS)
      {
        format::warning("ignoring SONG %zu.", i);
        continue;
      }

      DBM_song &sng = m.songs[i];

      if(!fread(sng.name, 44, 1, fp))
        return modutil::READ_ERROR;
      sng.name[44] = '\0';

      sng.num_orders = fget_u16be(fp);
      if(feof(fp))
        return modutil::READ_ERROR;

      sng.orders = new uint16_t[sng.num_orders];

      for(size_t i = 0; i < sng.num_orders; i++)
        sng.orders[i] = fget_u16be(fp);

      if(feof(fp))
        return modutil::READ_ERROR;
    }
    return modutil::SUCCESS;
  }
};

class PATT_handler
{
public:
  static constexpr IFFCode id = IFFCode("PATT");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    for(size_t i = 0; i < m.num_patterns; i++)
    {
      if(i >= MAX_PATTERNS)
      {
        format::warning("ignoring pattern %zu", i);
        continue;
      }
      if(len < 6)
      {
        format::error("pattern %zu header truncated.", i);
        return modutil::READ_ERROR;
      }

      DBM_pattern &p = m.patterns[i];

      p.num_rows         = fget_u16be(fp);
      p.packed_data_size = fget_u32be(fp);
      len -= 6;

      if(p.num_rows > 64)
        m.uses[FT_ROWS_OVER_64] = true;
      if(p.num_rows > 256)
        m.uses[FT_ROWS_OVER_256] = true;

      if(feof(fp))
        return modutil::READ_ERROR;

      if(len < p.packed_data_size)
      {
        format::error("pattern %zu truncated (left=%zu, expected>=%u).",
          i, len, p.packed_data_size);
        return modutil::READ_ERROR;
      }

      if(!p.num_rows)
      {
        if(p.packed_data_size)
          if(fseek(fp, p.packed_data_size, SEEK_CUR))
            return modutil::SEEK_ERROR;
        continue;
      }

      size_t num_notes = m.num_channels * p.num_rows;
      p.data = new DBM_pattern::note[num_notes]{};

      DBM_pattern::note *row = p.data;
      DBM_pattern::note *end = p.data + num_notes;
      ssize_t left = p.packed_data_size;

      while(left > 0 && row < end)
      {
        uint8_t channel = fgetc(fp);
        left--;

        if(!channel)
        {
          row += m.num_channels;
          continue;
        }

        uint8_t flags = fgetc(fp);
        left--;

        channel--;
        if(channel >= m.num_channels)
        {
          format::error("invalid pattern data.");
          return modutil::INVALID;
        }

        if(flags & DBM_pattern::NOTE)
        {
          row[channel].note = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::INSTRUMENT)
        {
          row[channel].instrument = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::EFFECT_1)
        {
          row[channel].effect_1 = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::PARAM_1)
        {
          row[channel].param_1 = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::EFFECT_2)
        {
          row[channel].effect_2 = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::PARAM_2)
        {
          row[channel].param_2 = fgetc(fp);
          left--;
        }

        if(feof(fp))
          return modutil::READ_ERROR;

        check_event(m, row[channel]);
      }
      if(left)
      {
        if(left < 0)
          format::warning("read %zd past end of packed data for pattern %zu.", -left, i);
        /* Don't print for 1 byte, this seems to be common... */
        if(left > 1)
          format::warning("%zd of packed data remaining for pattern %zu.", left, i);
        if(fseek(fp, left, SEEK_CUR))
          return modutil::SEEK_ERROR;
      }

      len -= p.packed_data_size;
    }
    return modutil::SUCCESS;
  }
};

class PNAM_handler
{
public:
  static constexpr IFFCode id = IFFCode("PNAM");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    m.pattern_names = true;
    m.pattern_name_encoding = fget_u16be(fp);

    ssize_t left = len;
    for(size_t i = 0; i < m.num_patterns; i++)
    {
      if(!left)
        break;

      uint8_t length = fgetc(fp);
      left--;

      if(left < length)
        break;

      DBM_pattern &p = m.patterns[i];

      p.name = new char[length + 1];
      if(!fread(p.name, length, 1, fp))
        return modutil::READ_ERROR;
      p.name[length] = '\0';
      left -= length;
    }
    return modutil::SUCCESS;
  }
};

class INST_handler
{
public:
  static constexpr IFFCode id = IFFCode("INST");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    if(len < 50 * m.num_instruments)
    {
      format::error("INST chunk length < %u", 50 * m.num_instruments);
      return modutil::INVALID;
    }

    for(size_t i = 0; i < m.num_instruments; i++)
    {
      if(i > MAX_INSTRUMENTS)
      {
        format::warning("ignoring instrument %zu.", i);
        continue;
      }

      DBM_instrument &is = m.instruments[i];

      if(!fread(is.name, 30, 1, fp))
        return modutil::READ_ERROR;
      is.name[30] = '\0';

      is.sample_id     = fget_u16be(fp);
      is.volume        = fget_u16be(fp);
      is.finetune_hz   = fget_u32be(fp);
      is.repeat_start  = fget_u32be(fp);
      is.repeat_length = fget_u32be(fp);
      is.panning       = fget_s16be(fp);
      is.flags         = fget_u16be(fp);
    }

    if(feof(fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
};

class SMPL_handler
{
public:
  static constexpr IFFCode id = IFFCode("SMPL");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    if(len < 8 * m.num_samples)
    {
      format::error("SMPL chunk length < %u.", 8 * m.num_samples);
      return modutil::INVALID;
    }

    for(size_t i = 0; i < m.num_samples; i++)
    {
      if(i >= MAX_SAMPLES)
      {
        format::warning("ignoring sample %zu.", i);
        continue;
      }

      DBM_sample &s = m.samples[i];

      s.flags  = fget_u32be(fp);
      s.length = fget_u32be(fp);

      size_t byte_length = s.length;
      if(s.flags & DBM_sample::S_8_BIT)
      {
        m.uses[FT_S_8_BIT] = true;
      }
      else

      if(s.flags & DBM_sample::S_16_BIT)
      {
        byte_length <<= 1;
        m.uses[FT_S_16_BIT] = true;
      }
      else

      if(s.flags & DBM_sample::S_32_BIT)
      {
        byte_length <<= 2;
        m.uses[FT_S_32_BIT] = true;
      }
      else
        m.uses[FT_S_UNKNOWN_FORMAT] = true;

      /* Ignore the sample data... */
      if(fseek(fp, byte_length, SEEK_CUR))
        return modutil::SEEK_ERROR;
    }
    return modutil::SUCCESS;
  }
};

static modutil::error read_envelope(DBM_data &m, DBM_envelope &env, size_t env_num, FILE *fp)
{
  env.instrument_id    = fget_u16be(fp);
  env.flags            = fgetc(fp);
  env.num_points       = fgetc(fp) + 1;
  env.sustain_1_point  = fgetc(fp);
  env.loop_start_point = fgetc(fp);
  env.loop_end_point   = fgetc(fp);
  env.sustain_2_point  = fgetc(fp);

  for(size_t i = 0; i < DBM_envelope::MAX_POINTS; i++)
  {
    DBM_envelope::point &p = env.points[i];
    p.time  = fget_u16be(fp);
    p.value = fget_s16be(fp);
    if(p.value < 0)
      m.uses[FT_NEGATIVE_ENVELOPE_VALUE] = true;
    if(p.value > 64)
      m.uses[FT_HIGH_ENVELOPE_VALUE] = true;
  }

  if(feof(fp))
    return modutil::READ_ERROR;

  if(env.instrument_id > m.num_instruments)
  {
    format::warning("envelope %zu for invalid instrument %u",
      env_num, env.instrument_id);
    return modutil::INVALID;
  }

  if(env.num_points > DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu for instrument %u contains too many points (%zu)",
      env_num, env.instrument_id, (size_t)env.num_points);
    return modutil::INVALID;
  }

  if(env.sustain_1_point >= DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu sustain 1 (%u) >= max points (32)",
      env_num, env.sustain_1_point);
    return modutil::INVALID;
  }

  if(env.sustain_2_point >= DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu sustain 2 (%u) >= max points (32)",
      env_num, env.sustain_2_point);
    return modutil::INVALID;
  }

  if(env.loop_start_point >= DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu loop start (%u) >= max points (32)",
      env_num, env.loop_start_point);
    return modutil::INVALID;
  }

  if(env.loop_end_point >= DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu loop end (%u) >= max points (32)",
      env_num, env.loop_end_point);
    return modutil::INVALID;
  }

  return modutil::SUCCESS;
}

class VENV_handler
{
public:
  static constexpr IFFCode id = IFFCode("VENV");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    m.uses[FT_VENV_CHUNK] = true;

    if(len < 4)
    {
      format::error("VENV chunk length < 4.");
      return modutil::INVALID;
    }

    uint16_t num_envelopes = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    if(!num_envelopes)
      return modutil::SUCCESS;

    m.num_volume_envelopes = num_envelopes;
    m.volume_envelopes = new DBM_envelope[num_envelopes];

    if(len < (size_t)(num_envelopes * 136 + 2))
    {
      format::warning("VENV chunk truncated (envelopes=%u, size=%zu, expected=%zu).",
        num_envelopes, len, (size_t)(2 + num_envelopes * 136));
      return modutil::SUCCESS;
    }

    for(size_t i = 0; i < num_envelopes; i++)
    {
      DBM_envelope &env = m.volume_envelopes[i];
      modutil::error result = read_envelope(m, env, i, fp);
      if(result == modutil::INVALID)
      {
        m.uses[FT_BAD_VOLUME_ENVELOPE] = true;
      }
      else

      if(result)
        return result;
    }
    return modutil::SUCCESS;
  }
};

class PENV_handler
{
public:
  static constexpr IFFCode id = IFFCode("PENV");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    m.uses[FT_PENV_CHUNK] = true;

    if(len < 4)
    {
      format::error("PENV chunk length < 4.");
      return modutil::INVALID;
    }

    uint16_t num_envelopes = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    if(!num_envelopes)
      return modutil::SUCCESS;

    m.num_pan_envelopes = num_envelopes;
    m.pan_envelopes = new DBM_envelope[num_envelopes];

    if(len < (size_t)(num_envelopes * 136 + 2))
    {
      format::warning("PENV chunk truncated (envelopes=%u, size=%zu, expected=%zu).",
        num_envelopes, len, (size_t)(2 + num_envelopes * 136));
      return modutil::SUCCESS;
    }

    for(size_t i = 0; i < num_envelopes; i++)
    {
      DBM_envelope &env = m.pan_envelopes[i];
      modutil::error result = read_envelope(m, env, i, fp);
      if(result == modutil::INVALID)
      {
        m.uses[FT_BAD_PAN_ENVELOPE] = true;
      }
      else

      if(result)
        return result;
    }
    return modutil::SUCCESS;
  }
};

class DSPE_handler
{
public:
  static constexpr IFFCode id = IFFCode("DSPE");

  static modutil::error parse(FILE *fp, size_t len, DBM_data &m)
  {
    m.uses[FT_DSPE_CHUNK] = true;

    if(len < 10)
    {
      format::error("DSPE chunk length < 10.");
      return modutil::INVALID;
    }

    m.dspe_mask_length = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    m.dspe_mask = new uint8_t[m.dspe_mask_length];
    if(!fread(m.dspe_mask, m.dspe_mask_length, 1, fp))
      return modutil::READ_ERROR;

    m.dspe_global_echo_delay    = fget_u16be(fp);
    m.dspe_global_echo_feedback = fget_u16be(fp);
    m.dspe_global_echo_mix      = fget_u16be(fp);
    m.dspe_cross_channel_echo   = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
};

static const IFF<
  DBM_data,
  NAME_handler,
  INFO_handler,
  SONG_handler,
  PATT_handler,
  PNAM_handler,
  INST_handler,
  SMPL_handler,
  VENV_handler,
  PENV_handler,
  DSPE_handler> DBM_parser;

static void print_envelopes(const char *name, size_t num, DBM_envelope *envs)
{
  // FIXME this needs to be a standard format.hpp thing if possible.
  if(Config.quiet)
    return;
  format::line();
  O_("%-6s  : Instr. #  Enabled : (...)=Loop  S=Sustain\n", name);
  O_("------  : --------  ------- : -------------------------\n");
  for(unsigned int i = 0; i < num; i++)
  {
    DBM_envelope &env = envs[i];
    size_t loop_start = (env.flags & DBM_envelope::LOOP)      ? env.loop_start_point : -1;
    size_t loop_end   = (env.flags & DBM_envelope::LOOP)      ? env.loop_end_point   : -1;
    size_t sustain_1  = (env.flags & DBM_envelope::SUSTAIN_1) ? env.sustain_1_point  : -1;
    size_t sustain_2  = (env.flags & DBM_envelope::SUSTAIN_2) ? env.sustain_2_point  : -1;

    O_("    %02x  : %-8u  %-7s : ",
      i + 1, env.instrument_id, (env.flags & DBM_envelope::ENABLED) ? "Yes" : "No"
    );
    for(size_t j = 0; j < env.num_points; j++)
    {
      fprintf(stderr, "%1s%-5u%1s ",
        (j == loop_start) ? "(" : "",
        env.points[j].time,
        (j == loop_end) ? ")" : ""
      );
    }
    fprintf(stderr, "\n");

    O_("        : %8s  %7s : ", "", "");
    for(size_t j = 0; j < env.num_points; j++)
    {
      fprintf(stderr, "%1s%-4d%1s%1s ",
        (j == loop_start) ? "(" : "",
        env.points[j].value,
        (j == sustain_1 || j == sustain_2) ? "S" : "",
        (j == loop_end) ? ")" : ""
      );
    }
    fprintf(stderr, "\n");
  }
}


class DBM_loader : public modutil::loader
{
public:
  DBM_loader(): modutil::loader("DBM", "dbm", "DigiBooster Pro") {}

  virtual modutil::error load(modutil::data state) const override
  {
    FILE *fp = state.reader.unwrap(); /* FIXME: */

    DBM_data m{};
    auto parser = DBM_parser;
    parser.max_chunk_length = 0;

    if(!fread(m.magic, 4, 1, fp))
      return modutil::FORMAT_ERROR;

    if(strncmp(m.magic, "DBM0", 4))
      return modutil::FORMAT_ERROR;

    total_dbm++;

    m.tracker_version = fget_u16be(fp);
    fget_u16be(fp);

    modutil::error err = parser.parse_iff(fp, 0, m);
    if(err)
      return err;

    if(parser.max_chunk_length > 4*1024*1024)
      m.uses[FT_CHUNK_OVER_4_MIB] = true;

    format::line("Name",      "%s", m.name_stripped);
    format::line("Type",      "DBM %d.%02x", m.tracker_version >> 8, m.tracker_version & 0xFF);
    format::line("Songs",     "%u", m.num_songs);
    if(m.num_samples)
      format::line("Samples", "%u", m.num_samples);
    if(m.num_instruments)
      format::line("Instr.",  "%u", m.num_instruments);
    if(m.num_volume_envelopes)
      format::line("V.Envs.", "%u", m.num_volume_envelopes);
    if(m.num_pan_envelopes)
      format::line("P.Envs.", "%u", m.num_pan_envelopes);
    format::line("Channels",  "%u", m.num_channels);
    format::line("Patterns",  "%u", m.num_patterns);
    format::line("MaxChunk",  "%zu", parser.max_chunk_length);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      if(m.num_samples)
      {
        static const char *labels[] = { "Type", "Length (samples)" };

        format::line();
        table::table<
          table::string<6>,
          table::number<16>> s_table;

        s_table.header("Samples", labels);
        for(unsigned int i = 0; i < m.num_samples; i++)
        {
          DBM_sample &s = m.samples[i];
          s_table.row(i + 1, s.type_str(), s.length);
        }
      }

      if(m.num_instruments)
      {
        static const char *labels[] =
        {
          "Name", "Sample #", "Vol", "Pan", "C4 Rate", "LoopStart", "LoopLen",
        };
        format::line();
        table::table<
          table::string<30>,
          table::spacer,
          table::number<8>,
          table::number<4>,
          table::number<4>,
          table::number<10>,
          table::spacer,
          table::number<10>,
          table::number<10>> i_table;

        i_table.header("Instr.", labels);
        for(unsigned int i = 0; i < m.num_instruments; i++)
        {
          DBM_instrument &is = m.instruments[i];
          i_table.row(i + 1, is.name, {},
           is.sample_id, is.volume, is.panning, is.finetune_hz, {},
           is.repeat_start, is.repeat_length);
        }
      }

      if(Config.dump_samples_extra && m.num_volume_envelopes)
      {
        print_envelopes("V.Env.", m.num_volume_envelopes, m.volume_envelopes);
      }

      if(Config.dump_samples_extra && m.num_pan_envelopes)
      {
        print_envelopes("P.Env.", m.num_pan_envelopes, m.pan_envelopes);
      }
    }

    if(Config.dump_patterns)
    {
      format::line();

      /* Print each song + order list. */
      for(unsigned int i = 0; i < m.num_songs; i++)
      {
        if(i >= MAX_SONGS)
          break;

        DBM_song &sng = m.songs[i];

        format::song("Song", "Orders", i + 1, sng.name, sng.orders, sng.num_orders);
        format::line();
      }

      for(unsigned int i = 0; i < m.num_patterns; i++)
      {
        if(i >= MAX_PATTERNS)
          break;

        DBM_pattern &p = m.patterns[i];
        const char *name = m.pattern_names ? p.name : nullptr;

        using EVENT = format::event<format::note<>, format::sample<>,
                                    format::effectXM, format::effectXM>;
        format::pattern<EVENT> pattern(name, i, m.num_channels, p.num_rows, p.packed_data_size);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        DBM_pattern::note *current = p.data;

        for(size_t row = 0; row < p.num_rows; row++)
        {
          for(size_t track = 0; track < m.num_channels; track++, current++)
          {
            format::note<>   a{ current->note };
            format::sample<> b{ current->instrument };
            format::effectXM c{ current->effect_1, current->param_1 };
            format::effectXM d{ current->effect_2, current->param_2 };

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
    if(!total_dbm)
      return;

    format::report("Total DBMs", total_dbm);
  }
};

static const DBM_loader loader;
