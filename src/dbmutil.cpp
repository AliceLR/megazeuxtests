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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "IFF.hpp"
#include "common.hpp"

static const char USAGE[] =
  "A utility to dump DBM metadata and patterns.\n"
  "Usage:\n"
  "  dbmutil [options] [filenames...]\n\n"
  "Options:\n"
  "  -s[=N]    Dump sample info. N=1 (optional) enables, N=0 disables (default).\n"
  "  -p[=N]    Dump patterns. N=1 (optional) enables, N=0 disables (default).\n"
  "            N=2 additionally dumps the entire pattern as raw data.\n"
  "  -         Read filenames from stdin. Useful when there are too many files\n"
  "            for argv. Place after any other options if applicable.\n\n";

static bool dump_samples      = false;
static bool dump_patterns     = false;
static bool dump_pattern_rows = false;

enum DBM_error
{
  DBM_SUCCESS,
  DBM_READ_ERROR,
  DBM_SEEK_ERROR,
  DBM_NOT_A_DBM,
  DBM_INVALID,
};

static const char *DBM_strerror(int err)
{
  switch(err)
  {
    case DBM_SUCCESS:           return "no error";
    case DBM_READ_ERROR:        return "read error";
    case DBM_SEEK_ERROR:        return "seek error";
    case DBM_NOT_A_DBM:         return "not a DigiBooster Pro module";
    case DBM_INVALID:           return "invalid DBM";
  }
  return IFF_strerror(err);
}

enum DBM_features
{
  FT_MULTIPLE_SONGS,
  FT_ROWS_OVER_256,
  FT_CHUNK_OVER_4_MIB,
  FT_VENV_CHUNK,
  FT_PENV_CHUNK,
  FT_DSPE_CHUNK,
  FT_BAD_VOLUME_ENVELOPE,
  FT_BAD_PAN_ENVELOPE,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  ">1Song",
  ">256Rows",
  ">4MBChunk",
  "VENV",
  "PENV",
  "DSPE",
  "BadVolEnv",
  "BadPanEnv",
};

static const int MAX_SONGS = 16;
static const int MAX_INSTRUMENTS = 256;
static const int MAX_SAMPLES = 256;
static const int MAX_PATTERNS = 256;

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

  static const uint8_t NOTE_SIZE       = 2;
  static const uint8_t INSTRUMENT_SIZE = NOTE_SIZE + 3;
  static const uint8_t EFFECT_1_SIZE   = INSTRUMENT_SIZE + 5;
  static const uint8_t EFFECT_2_SIZE   = EFFECT_1_SIZE + 5;

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
  uint8_t  *channel_size;
  note     *data;
  char     *name; /* From PNAM. */

  ~DBM_pattern()
  {
    delete[] channel_size;
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
    uint16_t volume;
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

static const class DBM_NAME_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_NAME_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 44)
    {
      O_("Error     : NAME chunk length %zu, expected >=44.\n", len);
      return DBM_INVALID;
    }
    if(m.read_name)
    {
      O_("Error    : duplicate NAME.\n");
      return DBM_INVALID;
    }

    if(!fread(m.name, 44, 1, fp))
      return DBM_READ_ERROR;

    m.name[44] = '\0';
    m.read_name = true;
    return DBM_SUCCESS;
  }
} NAME_handler("NAME", false);

static const class DBM_INFO_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_INFO_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 10)
    {
      O_("Error     : INFO chunk length %zu, expected >=10.\n", len);
      return DBM_INVALID;
    }
    if(m.read_info)
    {
      O_("Error     : duplicate INFO.\n");
      return DBM_INVALID;
    }

    m.num_instruments = fget_u16be(fp);
    m.num_samples     = fget_u16be(fp);
    m.num_songs       = fget_u16be(fp);
    m.num_patterns    = fget_u16be(fp);
    m.num_channels    = fget_u16be(fp);

    if(feof(fp))
      return DBM_READ_ERROR;

    return DBM_SUCCESS;
  }
} INFO_handler("INFO", false);

static const class DBM_SONG_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_SONG_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 46 * m.num_songs)
    {
      O_("Error     : SONG chunk length < %u\n", 46 * m.num_songs);
      return DBM_INVALID;
    }

    for(size_t i = 0; i < m.num_songs; i++)
    {
      if(i >= MAX_SONGS)
      {
        O_("Warning   : ignoring SONG %zu.\n", i);
        continue;
      }

      DBM_song &sng = m.songs[i];

      if(!fread(sng.name, 44, 1, fp))
        return DBM_READ_ERROR;
      sng.name[44] = '\0';

      sng.num_orders = fget_u16be(fp);
      if(feof(fp))
        return DBM_READ_ERROR;

      sng.orders = new uint16_t[sng.num_orders];

      for(size_t i = 0; i < sng.num_orders; i++)
        sng.orders[i] = fget_u16be(fp);

      if(feof(fp))
        return DBM_READ_ERROR;
    }
    return 0;
  }
} SONG_handler("SONG", false);

static const class DBM_PATT_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_PATT_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    for(size_t i = 0; i < m.num_patterns; i++)
    {
      if(i >= MAX_PATTERNS)
      {
        O_("Warning   : ignoring pattern %zu.\n", i);
        continue;
      }
      if(len < 6)
      {
        O_("Error     : pattern %zu header truncated.\n", i);
        return DBM_READ_ERROR;
      }

      DBM_pattern &p = m.patterns[i];

      p.num_rows         = fget_u16be(fp);
      p.packed_data_size = fget_u32be(fp);
      len -= 6;

      if(p.num_rows > 256)
        m.uses[FT_ROWS_OVER_256] = true;

      if(feof(fp))
        return DBM_READ_ERROR;

      if(len < p.packed_data_size)
      {
        O_("Error     : pattern %zu truncated (left=%zu, expected>=%u).\n",
          i, len, p.packed_data_size);
        return DBM_READ_ERROR;
      }

      if(!p.num_rows)
      {
        if(p.packed_data_size)
          if(fseek(fp, p.packed_data_size, SEEK_CUR))
            return DBM_SEEK_ERROR;
        continue;
      }

      size_t num_notes = m.num_channels * p.num_rows;
      p.data = new DBM_pattern::note[num_notes]{};
      p.channel_size = new uint8_t[m.num_channels]{};

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
          O_("Error     : invalid pattern data.\n");
          return DBM_INVALID;
        }

        uint8_t &sz = p.channel_size[channel];

        if(flags & DBM_pattern::NOTE)
        {
          row[channel].note = fgetc(fp);
          sz = MAX(sz, DBM_pattern::NOTE_SIZE);
          left--;
        }
        if(flags & DBM_pattern::INSTRUMENT)
        {
          row[channel].instrument = fgetc(fp);
          sz = MAX(sz, DBM_pattern::INSTRUMENT_SIZE);
          left--;
        }
        if(flags & DBM_pattern::EFFECT_1)
        {
          row[channel].effect_1 = fgetc(fp);
          sz = MAX(sz, DBM_pattern::EFFECT_1_SIZE);
          left--;
        }
        if(flags & DBM_pattern::PARAM_1)
        {
          row[channel].param_1 = fgetc(fp);
          sz = MAX(sz, DBM_pattern::EFFECT_1_SIZE);
          left--;
        }
        if(flags & DBM_pattern::EFFECT_2)
        {
          row[channel].effect_2 = fgetc(fp);
          sz = MAX(sz, DBM_pattern::EFFECT_2_SIZE);
          left--;
        }
        if(flags & DBM_pattern::PARAM_2)
        {
          row[channel].param_2 = fgetc(fp);
          sz = MAX(sz, DBM_pattern::EFFECT_2_SIZE);
          left--;
        }

        if(feof(fp))
          return DBM_READ_ERROR;
      }
      if(left)
      {
        if(left < 0)
          O_("Warning   : read %zd past end of packed data for pattern %zu.\n", -left, i);
        /* Don't print for 1 byte, this seems to be common... */
        if(left > 1)
          O_("Warning   : %zd of packed data remaining for pattern %zu.\n", left, i);
        if(fseek(fp, left, SEEK_CUR))
          return DBM_SEEK_ERROR;
      }

      len -= p.packed_data_size;
    }
    return DBM_SUCCESS;
  }
} PATT_handler("PATT", false);

static const class DBM_PNAM_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_PNAM_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    fgetc(fp); /* ??? */

    ssize_t left = len;
    for(size_t i = 0; i < m.num_patterns; i++)
    {
      if(left < 2)
        break;

      uint16_t length = fget_u16be(fp);
      left -= 2;

      if(left < length)
        break;

      DBM_pattern &p = m.patterns[i];

      p.name = new char[length + 1];
      if(!fread(p.name, length, 1, fp))
        return DBM_READ_ERROR;
      p.name[length] = '\0';
      left -= length;
    }
    return DBM_SUCCESS;
  }
} PNAM_handler("PNAM", false);

static const class DBM_INST_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_INST_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 50 * m.num_instruments)
    {
      O_("Error     : INST chunk length < %u\n", 50 * m.num_instruments);
      return DBM_INVALID;
    }

    for(size_t i = 0; i < m.num_instruments; i++)
    {
      if(i > MAX_INSTRUMENTS)
      {
        O_("Warning   : ignoring instrument %zu.\n", i);
        continue;
      }

      DBM_instrument &is = m.instruments[i];

      if(!fread(is.name, 30, 1, fp))
        return DBM_READ_ERROR;
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
      return DBM_READ_ERROR;

    return 0;
  }
} INST_handler("INST", false);

static const class DBM_SMPL_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_SMPL_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 8 * m.num_samples)
    {
      O_("Error     : SMPL chunk length < %u.\n", 8 * m.num_samples);
      return DBM_INVALID;
    }

    for(size_t i = 0; i < m.num_samples; i++)
    {
      if(i >= MAX_SAMPLES)
      {
        O_("Warning   : ignoring sample %zu.\n", i);
        continue;
      }

      DBM_sample &s = m.samples[i];

      s.flags  = fget_u32be(fp);
      s.length = fget_u32be(fp);

      size_t byte_length = s.length;
      if(s.flags & DBM_sample::S_16_BIT)
        byte_length <<= 1;
      else
      if(s.flags & DBM_sample::S_32_BIT)
        byte_length <<= 2;

      /* Ignore the sample data... */
      if(fseek(fp, byte_length, SEEK_CUR))
        return DBM_SEEK_ERROR;
    }
    return 0;
  }
} SMPL_handler("SMPL", false);

static int read_envelope(DBM_data &m, DBM_envelope &env, size_t env_num, FILE *fp)
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
    p.time   = fget_u16be(fp);
    p.volume = fget_u16be(fp);
  }

  if(feof(fp))
    return DBM_READ_ERROR;

  if(env.instrument_id > m.num_instruments)
  {
    O_("Warning   : envelope %zu for invalid instrument %u\n",
      env_num, env.instrument_id);
    return DBM_INVALID;
  }

  if(env.num_points > DBM_envelope::MAX_POINTS)
  {
    O_("Warning   : envelope %zu for instrument %u contains too many points (%zu)\n",
      env_num, env.instrument_id, (size_t)env.num_points);
    return DBM_INVALID;
  }

  if(env.sustain_1_point >= DBM_envelope::MAX_POINTS)
  {
    O_("Warning   : envelope %zu sustain 1 (%u) >= max points (32)\n",
      env_num, env.sustain_1_point);
    return DBM_INVALID;
  }

  if(env.sustain_2_point >= DBM_envelope::MAX_POINTS)
  {
    O_("Warning   : envelope %zu sustain 2 (%u) >= max points (32)\n",
      env_num, env.sustain_2_point);
    return DBM_INVALID;
  }

  if(env.loop_start_point >= DBM_envelope::MAX_POINTS)
  {
    O_("Warning   : envelope %zu loop start (%u) >= max points (32)\n",
      env_num, env.loop_start_point);
    return DBM_INVALID;
  }

  if(env.loop_end_point >= DBM_envelope::MAX_POINTS)
  {
    O_("Warning   : envelope %zu loop end (%u) >= max points (32)\n",
      env_num, env.loop_end_point);
    return DBM_INVALID;
  }

  return DBM_SUCCESS;
}

static const class DBM_VENV_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_VENV_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    m.uses[FT_VENV_CHUNK] = true;

    if(len < 4)
    {
      O_("Error     : VENV chunk length < 4.\n");
      return DBM_INVALID;
    }

    uint16_t num_envelopes = fget_u16be(fp);
    if(feof(fp))
      return DBM_READ_ERROR;

    if(!num_envelopes)
      return DBM_SUCCESS;

    m.num_volume_envelopes = num_envelopes;
    m.volume_envelopes = new DBM_envelope[num_envelopes];

    if(len < (size_t)(num_envelopes * 136 + 2))
    {
      O_("Error     : VENV chunk truncated (envelopes=%u, size=%zu, expected=%zu).\n",
        num_envelopes, len, (size_t)(2 + num_envelopes * 136));
      return DBM_SUCCESS;
    }

    for(size_t i = 0; i < num_envelopes; i++)
    {
      DBM_envelope &env = m.volume_envelopes[i];
      int result = read_envelope(m, env, i, fp);
      if(result == DBM_INVALID)
      {
        m.uses[FT_BAD_VOLUME_ENVELOPE] = true;
      }
      else

      if(result)
        return result;
    }
    return 0;
  }
} VENV_handler("VENV", false);

static const class DBM_PENV_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_PENV_Handler(const char *n, bool c): IFFHandler(n,c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    m.uses[FT_PENV_CHUNK] = true;

    if(len < 4)
    {
      O_("Error     : PENV chunk length < 4.\n");
      return DBM_INVALID;
    }

    uint16_t num_envelopes = fget_u16be(fp);
    if(feof(fp))
      return DBM_READ_ERROR;

    if(!num_envelopes)
      return DBM_SUCCESS;

    m.num_pan_envelopes = num_envelopes;
    m.pan_envelopes = new DBM_envelope[num_envelopes];

    if(len < (size_t)(num_envelopes * 136 + 2))
    {
      O_("Error     : PENV chunk truncated (envelopes=%u, size=%zu, expected=%zu).\n",
        num_envelopes, len, (size_t)(2 + num_envelopes * 136));
      return DBM_SUCCESS;
    }

    for(size_t i = 0; i < num_envelopes; i++)
    {
      DBM_envelope &env = m.pan_envelopes[i];
      int result = read_envelope(m, env, i, fp);
      if(result == DBM_INVALID)
      {
        m.uses[FT_BAD_PAN_ENVELOPE] = true;
      }
      else

      if(result)
        return result;
    }
    return DBM_SUCCESS;
  }
} PENV_handler("PENV", false);

static const class DBM_DSPE_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_DSPE_Handler(const char *n, bool c): IFFHandler(n,c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    m.uses[FT_DSPE_CHUNK] = true;

    if(len < 10)
    {
      O_("Error     : DSPE chunk length < 10.\n");
      return DBM_INVALID;
    }

    m.dspe_mask_length = fget_u16be(fp);
    if(feof(fp))
      return DBM_READ_ERROR;

    m.dspe_mask = new uint8_t[m.dspe_mask_length];
    if(!fread(m.dspe_mask, m.dspe_mask_length, 1, fp))
      return DBM_READ_ERROR;

    m.dspe_global_echo_delay    = fget_u16be(fp);
    m.dspe_global_echo_feedback = fget_u16be(fp);
    m.dspe_global_echo_mix      = fget_u16be(fp);
    m.dspe_cross_channel_echo   = fget_u16be(fp);
    if(feof(fp))
      return DBM_READ_ERROR;

    return DBM_SUCCESS;
  }
} DSPE_handler("DSPE", false);

static const IFF<DBM_data> DBM_parser({
  &NAME_handler,
  &INFO_handler,
  &SONG_handler,
  &PATT_handler,
  &PNAM_handler,
  &INST_handler,
  &SMPL_handler,
  &VENV_handler,
  &PENV_handler,
  &DSPE_handler,
});

static void print_envelopes(const char *name, size_t num, DBM_envelope *envs)
{
  O_("          :\n");
  O_("          : Instr. #  Enabled : (...)=Loop  [S]=Sustain\n");
  O_("          : --------  ------- : -------------------------\n");
  for(unsigned int i = 0; i < num; i++)
  {
    DBM_envelope &env = envs[i];
    size_t loop_start = (env.flags & DBM_envelope::LOOP)      ? env.loop_start_point : -1;
    size_t loop_end   = (env.flags & DBM_envelope::LOOP)      ? env.loop_end_point   : -1;
    size_t sustain_1  = (env.flags & DBM_envelope::SUSTAIN_1) ? env.sustain_1_point  : -1;
    size_t sustain_2  = (env.flags & DBM_envelope::SUSTAIN_2) ? env.sustain_2_point  : -1;

    O_("%-6s %02x : %-8u  %-7s : ",
      name, i + 1, env.instrument_id, (env.flags & DBM_envelope::ENABLED) ? "Yes" : "No"
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

    O_("          : %8s  %7s : ", "", "");
    for(size_t j = 0; j < env.num_points; j++)
    {
      fprintf(stderr, "%1s%-2u%3s%1s ",
        (j == loop_start) ? "(" : "",
        env.points[j].volume,
        (j == sustain_1 || j == sustain_2) ? "[S]" : "",
        (j == loop_end) ? ")" : ""
      );
    }
    fprintf(stderr, "\n");
  }
}

static void print_pattern_head(DBM_data &m, DBM_pattern &p)
{
  fprintf(stderr, "\n");
  O_("");
  for(unsigned int i = 0; i < m.num_channels; i++)
  {
    int size = p.channel_size[i];
    if(size)
      fprintf(stderr, " %02x%*s :", i, size - 2, "");
  }
  fprintf(stderr, "\n");
  O_("");
  for(size_t i = 0; i < m.num_channels; i++)
  {
    int size = p.channel_size[i];
    if(size)
      fprintf(stderr, " %.*s :", size, "--------------------");
  }
  fprintf(stderr, "\n");
}

static void print_pattern_notes(DBM_data &m, DBM_pattern &p)
{
  DBM_pattern::note *current = p.data;

  for(size_t i = 0; i < p.num_rows; i++)
  {
    O_("");
    for(size_t j = 0; j < m.num_channels; j++, current++)
    {
      uint8_t size = p.channel_size[j];
      if(size >= DBM_pattern::NOTE_SIZE)
      {
        if(current->note)
          fprintf(stderr, " %02x", current->note);
        else
          fprintf(stderr, "   ");
      }
      if(size >= DBM_pattern::INSTRUMENT_SIZE)
      {
        if(current->instrument)
          fprintf(stderr, " %02x", current->instrument);
        else
          fprintf(stderr, "   ");
      }
      if(size >= DBM_pattern::EFFECT_1_SIZE)
      {
        if(current->effect_1 || current->param_1)
          fprintf(stderr, " %2x%02x", current->effect_1, current->param_1);
        else
          fprintf(stderr, "     ");
      }
      if(size >= DBM_pattern::EFFECT_2_SIZE)
      {
        if(current->effect_2 || current->param_2)
          fprintf(stderr, " %2x%02x", current->effect_2, current->param_2);
        else
          fprintf(stderr, "     ");
      }
      if(size)
        fprintf(stderr, " :");
    }
    fprintf(stderr, "\n");
  }
}

static int DBM_read(FILE *fp)
{
  DBM_data m{};
  DBM_parser.max_chunk_length = 0;

  if(!fread(m.magic, 4, 1, fp))
    return DBM_READ_ERROR;

  if(strncmp(m.magic, "DBM0", 4))
    return DBM_NOT_A_DBM;

  m.tracker_version = fget_u16be(fp);
  fget_u16be(fp);

  int err = DBM_parser.parse_iff(fp, 0, m);
  if(err)
    return err;

  if(DBM_parser.max_chunk_length > 4*1024*1024)
    m.uses[FT_CHUNK_OVER_4_MIB] = true;

  O_("Name      : %s\n",  m.name);
  O_("Songs     : %u\n",  m.num_songs);
  if(m.num_samples)
    O_("Samples   : %u\n",  m.num_samples);
  if(m.num_instruments)
    O_("Instr.    : %u\n",  m.num_instruments);
  if(m.num_volume_envelopes)
    O_("V.Envs.   : %u\n", m.num_volume_envelopes);
  if(m.num_pan_envelopes)
    O_("P.Envs.   : %u\n", m.num_pan_envelopes);
  O_("Channels  : %u\n",  m.num_channels);
  O_("Patterns  : %u\n",  m.num_patterns);
  O_("Max Chunk : %zu\n", DBM_parser.max_chunk_length);

  O_("Uses      :");
  for(int i = 0; i < NUM_FEATURES; i++)
    if(m.uses[i])
      fprintf(stderr, " %s", FEATURE_STR[i]);
  fprintf(stderr, "\n");

  if(dump_samples)
  {
    if(m.num_samples)
    {
      O_("          :\n");
      O_("          : Type    Length (samples)\n");
      O_("          : ------  ----------------\n");
      for(unsigned int i = 0; i < m.num_samples; i++)
      {
        DBM_sample &s = m.samples[i];
        O_("Sample %02x : %-6s  %-u\n", i + 1, s.type_str(), s.length);
      }
    }

    if(m.num_instruments)
    {
      O_("          :\n");
      O_("          : Sample #  D.Vol  Pan    C4 Rate    : Loop Start  Loop Len.  \n");
      O_("          : --------  -----  -----  ---------- : ----------  ---------- \n");
      for(unsigned int i = 0; i < m.num_instruments; i++)
      {
        DBM_instrument &is = m.instruments[i];
        O_("Instr. %02x : %-8u  %-5u  %-5d  %-10u : %-10u %-10u\n",
          i + 1, is.sample_id, is.volume, is.panning, is.finetune_hz,
          is.repeat_start, is.repeat_length
        );
      }
    }

    if(m.num_volume_envelopes)
    {
      print_envelopes("V.Env.", m.num_volume_envelopes, m.volume_envelopes);
    }

    if(m.num_pan_envelopes)
    {
      print_envelopes("P.Env.", m.num_pan_envelopes, m.pan_envelopes);
    }
  }

  if(dump_patterns)
  {
    O_("          :\n");

    /* Print each song + order list. */
    for(unsigned int i = 0; i < m.num_songs; i++)
    {
      if(i >= MAX_SONGS)
        break;

      DBM_song &sng = m.songs[i];

      O_("Song %02x   : '%s' (%u orders)\n", i + 1, sng.name, sng.num_orders);
      O_("          :");
      for(size_t j = 0; j < sng.num_orders; j++)
        fprintf(stderr, " %02x", sng.orders[j]);
      fprintf(stderr, "\n");
      O_("          :\n");
    }

    for(unsigned int i = 0; i < m.num_patterns; i++)
    {
      if(i >= MAX_PATTERNS)
        break;

      DBM_pattern &p = m.patterns[i];

      if(dump_pattern_rows)
        fprintf(stderr, "\n");

      O_("Pattern %02x: %u rows, %u bytes\n", i, p.num_rows, p.packed_data_size);

      if(dump_pattern_rows)
      {
        print_pattern_head(m, p);
        print_pattern_notes(m, p);
      }
    }
  }
  return DBM_SUCCESS;
}

void check_dbm(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("File      : %s\n", filename);

    int err = DBM_read(fp);
    if(err)
      O_("Error     : %s\n\n", DBM_strerror(err));
    else
      fprintf(stderr, "\n");

    fclose(fp);
  }
  else
    O_("Error     : failed to open '%s'.\n", filename);
}

int main(int argc, char *argv[])
{
  bool read_stdin = false;

  if(!argv || argc < 2)
  {
    fprintf(stdout, "%s", USAGE);
    return 0;
  }

  for(int i = 1; i < argc; i++)
  {
    char *arg = argv[i];
    if(arg[0] == '-')
    {
      switch(arg[1])
      {
        case '\0':
          if(!read_stdin)
          {
            char buffer[1024];
            while(fgets_safe(buffer, stdin))
              check_dbm(buffer);

            read_stdin = true;
          }
          continue;

        case 'p':
          if(!arg[2] || !strcmp(arg + 2, "=1"))
          {
            dump_patterns = true;
            dump_pattern_rows = false;
            continue;
          }
          if(!strcmp(arg + 2, "=2"))
          {
            dump_patterns = true;
            dump_pattern_rows = true;
            continue;
          }
          if(!strcmp(arg + 2, "=0"))
          {
            dump_patterns = false;
            dump_pattern_rows = false;
            continue;
          }
          break;

        case 's':
          if(!arg[2] || !strcmp(arg + 2, "=1"))
          {
            dump_samples = true;
            continue;
          }
          if(!strcmp(arg + 2, "=0"))
          {
            dump_samples = false;
            continue;
          }
          break;
      }
    }
    check_dbm(arg);
  }

  return 0;
}
