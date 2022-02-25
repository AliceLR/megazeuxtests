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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

static int num_xms;


enum XM_features
{
  FT_SAMPLE_STEREO,
  FT_SAMPLE_16,
  FT_SAMPLE_ADPCM,
  FT_SAMPLE_OGG,
  FT_ORDER_OVER_NUM_PATTERNS,
  FT_ORDER_FE,
  FT_ORDER_FE_MODPLUG_SKIP,
  FT_MODPLUG_FILTER,
  FT_FX_UNKNOWN,
  FT_EX_UNKNOWN,
  FT_XX_UNKNOWN,
  FT_XX_REVERSE,
  FT_FX_MODPLUG_EXTENSION,
  FT_FX_ENVELOPE_POSITION,
  FT_FX_UNUSED_I,
  FT_FX_UNUSED_J,
  FT_FX_UNUSED_M,
  FT_FX_UNUSED_N,
  FT_FX_UNUSED_O,
  FT_FX_UNUSED_Q,
  FT_FX_UNUSED_S,
  FT_FX_UNUSED_U,
  FT_FX_UNUSED_V,
  FT_FX_UNUSED_W,
  NUM_FEATURES
};

static constexpr const char *FEATURE_STR[NUM_FEATURES] =
{
  "S:Stereo",
  "S:16",
  "S:ADPCM",
  "S:Ogg",
  "O>NumPat",
  "O:FE",
  "MPT:FE",
  "MPT:Filter",
  "E:?xx",
  "E:E?x",
  "E:X?x",
  "E:Reverse",
  "E:MPT",
  "E:EnvPos",
  "E:Ixx",
  "E:Jxx",
  "E:Mxx",
  "E:Nxx",
  "E:Oxx",
  "E:Qxx",
  "E:Sxx",
  "E:Uxx",
  "E:Vxx",
  "E:Wxx",
};

static constexpr int MAX_CHANNELS = 256;
static constexpr int MAX_ORDERS = 256;
static constexpr int MAX_PATTERNS = 256;

enum XM_effect
{
  FX_ARPEGGIO = 0,
  FX_PORTAMENTO_UP,
  FX_PORTAMENTO_DOWN,
  FX_TONE_PORTAMENTO,
  FX_VIBRATO,
  FX_PORTAMENTO_VOLSLIDE,
  FX_VIBRATO_VOLSLIDE,
  FX_TREMOLO,
  FX_PAN,
  FX_OFFSET,
  FX_VOLSLIDE,
  FX_JUMP,
  FX_VOLUME,
  FX_BREAK,
  FX_EXTRA,
  FX_SPEED_TEMPO,
  FX_GLOBAL_VOLUME,
  FX_GLOBAL_VOLSLIDE,
  FX_UNUSED_I,
  FX_UNUSED_J,
  FX_KEY_OFF,
  FX_ENVELOPE_POSITION,
  FX_UNUSED_M,
  FX_UNUSED_N,
  FX_UNUSED_O,
  FX_PAN_SLIDE,
  FX_UNUSED_Q,     // May be used to set filter resonance in "rst's SoundTracker".
  FX_MULTI_RETRIGGER,
  FX_UNUSED_S,
  FX_TREMOR,
  FX_UNUSED_U,
  FX_UNUSED_V,
  FX_UNUSED_W,
  FX_EXTRA_2,
  FX_PANBRELLO,    // ModPlug extension
  FX_MACRO,        // ModPlug extension, also may set filter cutoff in "rst's SoundTracker".
  FX_SMOOTH_MACRO, // ModPlug extension

  EX_UNUSED_0 = 0,
  EX_FINE_PORTAMENTO_UP,
  EX_FINE_PORTAMENTO_DOWN,
  EX_GLISSANDO_CONTROL,
  EX_VIBRATO_CONTROL,
  EX_FINETUNE,
  EX_LOOP,
  EX_TREMOLO_CONTROL,
  EX_PAN,
  EX_RETRIGGER,
  EX_FINE_VOLSLIDE_UP,
  EX_FINE_VOLSLIDE_DOWN,
  EX_NOTE_CUT,
  EX_NOTE_DELAY,
  EX_PATTERN_DELAY,
  EX_SET_ACTIVE_MACRO, // ModPlug extension

  XX_UNUSED_0 = 0,
  XX_EXTRA_FINE_PORTAMENTO_UP,
  XX_EXTRA_FINE_PORTAMENTO_DOWN,
  XX_UNUSED_3,
  XX_UNUSED_4,
  XX_PANBRELLO_CONTROL,  // ModPlug extension
  XX_FINE_PATTERN_DELAY, // ModPlug extension
  XX_UNUSED_7,
  XX_UNUSED_8,
  XX_SOUND_CONTROL,      // ModPlug extension
  XX_HIGH_OFFSET,        // ModPlug extension
};

struct XM_header
{
  /*  00 */ char magic[17];       // 'Extended Module: '
  /*  17 */ char name[20];        // Null-padded, not null-terminated.
  /*  37 */ uint8_t eof;          // 0x1a
  /*  38 */ char tracker[20];     // Tracker name.
  /*  58 */ uint16_t version;     // Format version; hi-byte: major, lo-byte: minor.
  /*  60 */ uint32_t header_size;
  /*  64 */ uint16_t num_orders;
  /*  66 */ uint16_t restart_pos;
  /*  68 */ uint16_t num_channels;
  /*  70 */ uint16_t num_patterns;
  /*  72 */ uint16_t num_instruments;
  /*  74 */ uint16_t flags;
  /*  76 */ uint16_t default_tempo;
  /*  78 */ uint16_t default_bpm;
  /*  80 */ uint8_t orders[MAX_ORDERS];
  /* 336 */
};

struct XM_event
{
  enum
  {
    NOTE       = (1<<0),
    INSTRUMENT = (1<<1),
    VOLUME     = (1<<2),
    EFFECT     = (1<<3),
    PARAM      = (1<<4),
    PACKED     = (1<<7),
  };

  uint8_t note = 0;
  uint8_t instrument = 0;
  uint8_t volume = 0;
  uint8_t effect = 0;
  uint8_t param = 0;

  XM_event() {}
  XM_event(uint8_t *&stream, uint8_t *end)
  {
    uint8_t flags = (stream < end) ? *stream : 0;
    stream++;

    if(flags & PACKED)
    {
      if(flags & NOTE)
      {
        note = (stream < end) ? *stream : 0;
        stream++;
      }
      if(flags & INSTRUMENT)
      {
        instrument = (stream < end) ? *stream : 0;
        stream++;
      }
      if(flags & VOLUME)
      {
        volume = (stream < end) ? *stream : 0;
        stream++;
      }
      if(flags & EFFECT)
      {
        effect = (stream < end) ? *stream : 0;
        stream++;
      }
      if(flags & PARAM)
      {
        param = (stream < end) ? *stream : 0;
        stream++;
      }
    }
    else
    {
      note = flags;
      if(stream + 4 <= end)
      {
        instrument = stream[0];
        volume     = stream[1];
        effect     = stream[2];
        param      = stream[3];
      }
      stream += 4;
    }
  }
};

struct XM_pattern
{
  XM_event *events = nullptr;
  uint32_t header_size; /* should be 9 */
  uint8_t  packing_type;
  uint16_t num_rows;
  uint16_t packed_size;

  ~XM_pattern()
  {
    delete[] events;
  }

  void allocate(uint16_t channels, uint16_t rows)
  {
    events = new XM_event[channels * rows]{};
  }
};

struct XM_sample
{
  enum XM_sample_type
  {
    LOOP   = (1<<0),
    BIDI   = (1<<1),
    S16    = (1<<4),
    STEREO = (1<<5),
    // Used in the reserved field.
    ADPCM  = 0xad,
  };

  /*  0 */ uint32_t length;
  /*  4 */ uint32_t loop_start;
  /*  8 */ uint32_t loop_length;
  /* 12 */ uint8_t  volume;
  /* 13 */ int8_t   finetune;
  /* 14 */ uint8_t  type;
  /* 15 */ uint8_t  panning;
  /* 16 */ int8_t   transpose;
  /* 17 */ uint8_t  reserved;
  /* 18 */ char     name[22];
  /* 40 */
};

struct XM_instrument
{
  XM_sample *samples = nullptr;

  /*   0 */ uint32_t header_size;
  /*   4 */ char     name[22];
  /*  26 */ uint8_t  type;
  /*  27 */ uint16_t num_samples;

  /*  29 */ uint32_t sample_header_size;
  /*  33 */ uint8_t  keymap[96];
  /* 129 */ uint8_t  volume_points[48];
  /* 177 */ uint8_t  pan_points[48];
  /* 225 */ uint8_t  num_volume_points;
  /* 226 */ uint8_t  num_pan_points;
  /* 227 */ uint8_t  volume_sustain;
  /* 228 */ uint8_t  volume_loop_start;
  /* 229 */ uint8_t  volume_loop_end;
  /* 230 */ uint8_t  pan_sustain;
  /* 231 */ uint8_t  pan_loop_start;
  /* 232 */ uint8_t  pan_loop_end;
  /* 233 */ uint8_t  volume_type;
  /* 234 */ uint8_t  pan_type;
  /* 235 */ uint8_t  vibrato_type;
  /* 236 */ uint8_t  vibrato_sweep;
  /* 237 */ uint8_t  vibrato_depth;
  /* 238 */ uint8_t  vibrato_rate;
  /* 239 */ uint16_t fadeout;
  /* 241 */ uint16_t reserved;
  /* 243 */

  ~XM_instrument()
  {
    delete[] samples;
  }

  void allocate()
  {
    samples = new XM_sample[num_samples]{};
  }
};

struct XM_data
{
  XM_header     header;
  XM_pattern    patterns[256];
  XM_instrument *instruments = nullptr;
  uint8_t       *buffer = nullptr;

  char name[21];
  char tracker[21];
  size_t num_samples;
  bool uses[NUM_FEATURES];

  ~XM_data()
  {
    delete[] instruments;
    delete[] buffer;
  }

  void allocate_buffer()
  {
    buffer = new uint8_t[65536];
  }

  void allocate_instruments()
  {
    instruments = new XM_instrument[header.num_instruments]{};
  }
};


static void check_event(XM_data &m, const XM_event *ev)
{
  switch(ev->effect)
  {
    case FX_ARPEGGIO:
    case FX_PORTAMENTO_UP:
    case FX_PORTAMENTO_DOWN:
    case FX_TONE_PORTAMENTO:
    case FX_VIBRATO:
    case FX_PORTAMENTO_VOLSLIDE:
    case FX_VIBRATO_VOLSLIDE:
    case FX_TREMOLO:
    case FX_PAN:
    case FX_OFFSET:
    case FX_VOLSLIDE:
    case FX_JUMP:
    case FX_VOLUME:
    case FX_BREAK:
    case FX_SPEED_TEMPO:
    case FX_GLOBAL_VOLUME:
    case FX_GLOBAL_VOLSLIDE:
    case FX_KEY_OFF:
      break;

    case FX_ENVELOPE_POSITION:
      m.uses[FT_FX_ENVELOPE_POSITION] = true;
      break;

    case FX_PAN_SLIDE:
    case FX_MULTI_RETRIGGER:
    case FX_TREMOR:
      break;

    case FX_PANBRELLO:
      m.uses[FT_FX_MODPLUG_EXTENSION] = true;
      break;

    case FX_MACRO:
    case FX_SMOOTH_MACRO:
      m.uses[FT_FX_MODPLUG_EXTENSION] = true;
      m.uses[FT_MODPLUG_FILTER] = true;
      break;

    // Unknown effects found in real modules.
    case FX_UNUSED_I:
      m.uses[FT_FX_UNUSED_I] = true;
      break;
    case FX_UNUSED_J:
      m.uses[FT_FX_UNUSED_J] = true;
      break;
    case FX_UNUSED_M:
      m.uses[FT_FX_UNUSED_M] = true;
      break;
    case FX_UNUSED_N:
      m.uses[FT_FX_UNUSED_N] = true;
      break;
    case FX_UNUSED_O:
      m.uses[FT_FX_UNUSED_O] = true;
      break;
    case FX_UNUSED_Q:
      m.uses[FT_FX_UNUSED_Q] = true;
      break;
    case FX_UNUSED_S:
      m.uses[FT_FX_UNUSED_S] = true;
      break;
    case FX_UNUSED_U:
      m.uses[FT_FX_UNUSED_U] = true;
      break;
    case FX_UNUSED_V:
      m.uses[FT_FX_UNUSED_V] = true;
      break;
    case FX_UNUSED_W:
      m.uses[FT_FX_UNUSED_W] = true;
      break;

    // Extra effects (Exx and Xxx)
    case FX_EXTRA:
    {
      switch(ev->param >> 4)
      {
        case EX_UNUSED_0:
        case EX_FINE_PORTAMENTO_UP:
        case EX_FINE_PORTAMENTO_DOWN:
        case EX_GLISSANDO_CONTROL:
        case EX_VIBRATO_CONTROL:
        case EX_FINETUNE:
        case EX_LOOP:
        case EX_TREMOLO_CONTROL:
        case EX_PAN:
        case EX_RETRIGGER:
        case EX_FINE_VOLSLIDE_UP:
        case EX_FINE_VOLSLIDE_DOWN:
        case EX_NOTE_CUT:
        case EX_NOTE_DELAY:
        case EX_PATTERN_DELAY:
          break;

        case EX_SET_ACTIVE_MACRO:
          m.uses[FT_FX_MODPLUG_EXTENSION] = true;
          m.uses[FT_MODPLUG_FILTER] = true;
          break;

        default:
          m.uses[FT_EX_UNKNOWN] = true;
          break;
      }
      break;
    }

    case FX_EXTRA_2:
    {
      switch(ev->param >> 4)
      {
        case XX_UNUSED_0:
        case XX_EXTRA_FINE_PORTAMENTO_UP:
        case XX_EXTRA_FINE_PORTAMENTO_DOWN:
          break;

        case XX_PANBRELLO_CONTROL:
        case XX_FINE_PATTERN_DELAY:
        case XX_HIGH_OFFSET:
          m.uses[FT_FX_MODPLUG_EXTENSION] = true;
          break;

        case XX_SOUND_CONTROL:
          m.uses[FT_FX_MODPLUG_EXTENSION] = true;
          if((ev->param & 0xf) >= 0xe)
            m.uses[FT_XX_REVERSE] = true;
          break;

        default:
          m.uses[FT_XX_UNKNOWN] = true;
          break;
      }
      break;
    }

    default:
      m.uses[FT_FX_UNKNOWN] = true;
      break;
  }
}

static modutil::error load_patterns(XM_data &m, FILE *fp)
{
  m.allocate_buffer();

  for(size_t i = 0; i < m.header.num_patterns; i++)
  {
    XM_pattern &p = m.patterns[i];
    size_t version_header_size = (m.header.version >= 0x0103) ? 9 : 8;

    p.header_size = fget_u32le(fp);
    if(p.header_size < version_header_size)
    {
      format::error("invalid pattern %zu header size = %" PRIu32, i, p.header_size);
      return modutil::INVALID;
    }

    p.packing_type = fgetc(fp);

    if(m.header.version >= 0x0103)
      p.num_rows = fget_u16le(fp);
    else
      p.num_rows = fgetc(fp) + 1;

    p.packed_size = fget_u16le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    // Skip any remaining header.
    if(p.header_size > version_header_size)
      if(fseek(fp, p.header_size - version_header_size, SEEK_CUR))
        return modutil::SEEK_ERROR;

    p.allocate(m.header.num_channels, p.num_rows);
    if(!p.packed_size)
      continue;

    uint8_t *buffer = m.buffer;
    if(!fread(buffer, p.packed_size, 1, fp))
    {
      format::error("read error at pattern %zu", i);
      return modutil::READ_ERROR;
    }

    XM_event *ev = p.events;
    uint8_t *current = buffer;
    uint8_t *end = buffer + p.packed_size;

    for(size_t row = 0; row < p.num_rows; row++)
    {
      for(size_t track = 0; track < m.header.num_channels; track++, ev++)
      {
        *ev = XM_event(current, end);
        if(current > end)
        {
          format::warning("invalid pattern packing for %zu", i);
          return modutil::INVALID;
        }
        check_event(m, ev);
      }
    }
  }
  return modutil::SUCCESS;
}

static modutil::error load_instruments(XM_data &m, FILE *fp)
{
  uint8_t buffer[243];

  m.allocate_instruments();

  for(size_t i = 0; i < m.header.num_instruments; i++)
  {
    XM_instrument &ins = m.instruments[i];

    if(!fread(buffer, 29, 1, fp))
    {
      format::error("read error at instrument %zu", i);
      return modutil::READ_ERROR;
    }

    ins.header_size = mem_u32le(buffer + 0);
    ins.type        = buffer[26];
    ins.num_samples = mem_u16le(buffer + 27);

    if(ins.header_size < 29 || (ins.num_samples && ins.header_size < 243))
    {
      format::error("invalid instrument %zu header size = %" PRIu32, i, ins.header_size);
      return modutil::INVALID;
    }

    memcpy(ins.name, buffer + 4, sizeof(ins.name));

    m.num_samples += ins.num_samples;
    if(!ins.num_samples)
    {
      // Skip any remaining header.
      if(ins.header_size > 29)
        if(fseek(fp, ins.header_size - 29, SEEK_CUR))
          return modutil::SEEK_ERROR;

      continue;
    }

    if(!fread(buffer + 29, (243 - 29), 1, fp))
    {
      format::error("read error at instrument %zu", i);
      return modutil::READ_ERROR;
    }

    memcpy(ins.keymap, buffer + 33, sizeof(ins.keymap));
    memcpy(ins.volume_points, buffer + 129, sizeof(ins.volume_points));
    memcpy(ins.pan_points, buffer + 177, sizeof(ins.pan_points));

    ins.sample_header_size = mem_u32le(buffer + 29);
    ins.num_volume_points  = buffer[225];
    ins.num_pan_points     = buffer[226];
    ins.volume_sustain     = buffer[227];
    ins.volume_loop_start  = buffer[228];
    ins.volume_loop_end    = buffer[229];
    ins.pan_sustain        = buffer[230];
    ins.pan_loop_start     = buffer[231];
    ins.pan_loop_end       = buffer[232];
    ins.volume_type        = buffer[233];
    ins.pan_type           = buffer[234];
    ins.vibrato_type       = buffer[235];
    ins.vibrato_sweep      = buffer[236];
    ins.vibrato_depth      = buffer[237];
    ins.vibrato_rate       = buffer[238];
    ins.fadeout            = mem_u16le(buffer + 239);
    ins.reserved           = mem_u16le(buffer + 241);

    if(ins.sample_header_size < 40)
    {
      format::error("invalid instrument %zu sample header size = %" PRIu32, i, ins.sample_header_size);
      return modutil::INVALID;
    }

    // Skip any remaining header.
    if(ins.header_size > 243)
      if(fseek(fp, ins.header_size - 243, SEEK_CUR))
        return modutil::SEEK_ERROR;

    ins.allocate();

    size_t sample_total_length = 0;
    for(size_t j = 0; j < ins.num_samples; j++)
    {
      XM_sample &s = ins.samples[j];

      if(!fread(buffer, 40, 1, fp))
      {
        format::error("read error at instrument %zu sample %zu", i, j);
        return modutil::READ_ERROR;
      }

      s.length      = mem_u32le(buffer + 0);
      s.loop_start  = mem_u32le(buffer + 4);
      s.loop_length = mem_u32le(buffer + 8);
      s.volume      = buffer[12];
      s.finetune    = buffer[13];
      s.type        = buffer[14];
      s.panning     = buffer[15];
      s.transpose   = buffer[16];
      s.reserved    = buffer[17];
      memcpy(s.name, buffer + 18, sizeof(s.name));

      // Skip any remaining header.
      if(ins.sample_header_size > 40)
        if(fseek(fp, ins.sample_header_size - 40, SEEK_CUR))
          return modutil::SEEK_ERROR;

      if(s.type & XM_sample::STEREO)
        m.uses[FT_SAMPLE_STEREO] = true;

      if(s.type & XM_sample::S16)
        m.uses[FT_SAMPLE_16] = true;

      if(s.reserved == XM_sample::ADPCM)
      {
        m.uses[FT_SAMPLE_ADPCM] = true;
        sample_total_length += ((s.length + 1) >> 1) /* Compressed size */ + 16 /* ADPCM table */;
      }
      else
        sample_total_length += s.length;
    }

    // NOTE: skip sample data after sample headers ONLY for >=0x0104.
    // Prior versions store them all at the very end of the module.
    if(m.header.version >= 0x0104)
    {
      char tmp[8];
      if(fread(tmp, 8, 1, fp))
      {
        if(!memcmp(tmp + 4, "OggS", 4))
          m.uses[FT_SAMPLE_OGG] = true;

        sample_total_length -= 8;
      }
      if(fseek(fp, sample_total_length, SEEK_CUR))
        return modutil::SEEK_ERROR;
    }
  }
  return modutil::SUCCESS;
}


class XM_loader : public modutil::loader
{
public:
  XM_loader(): modutil::loader("XM", "xm", "Extended Module") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    XM_data m{};
    XM_header &h = m.header;
    bool invalid = false;
    bool mpt_extension = false;
    bool has_fe = false;

    if(!fread(h.magic, sizeof(h.magic), 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(h.magic, "Extended Module: ", 17))
      return modutil::FORMAT_ERROR;

    num_xms++;

    /* Header */

    if(!fread(h.name, sizeof(h.name), 1, fp))
      return modutil::READ_ERROR;

    memcpy(m.name, h.name, sizeof(h.name));
    m.name[sizeof(h.name)] = '\0';
    strip_module_name(m.name, sizeof(m.name));

    h.eof = fgetc(fp);

    if(!fread(h.tracker, sizeof(h.tracker), 1, fp))
      return modutil::READ_ERROR;

    memcpy(m.tracker, h.tracker, sizeof(h.tracker));
    m.tracker[sizeof(h.tracker)] = '\0';
    strip_module_name(m.tracker, sizeof(m.tracker));

    h.version     = fget_u16le(fp);
    h.header_size = fget_u32le(fp);
    if(h.header_size <= 20)
    {
      format::error("header size must be >20; is %" PRIu32, h.header_size);
      return modutil::INVALID;
    }

    h.num_orders      = fget_u16le(fp);
    h.restart_pos     = fget_u16le(fp);
    h.num_channels    = fget_u16le(fp);
    h.num_patterns    = fget_u16le(fp);
    h.num_instruments = fget_u16le(fp);
    h.flags           = fget_u16le(fp);
    h.default_tempo   = fget_u16le(fp);
    h.default_bpm     = fget_u16le(fp);

    if(h.num_channels > MAX_CHANNELS)
    {
      format::error("invalid channel count %u > 256", h.num_channels);
      return modutil::INVALID;
    }

    if(h.num_orders > MAX_ORDERS)
    {
      format::error("invalid order count %u > 256", h.num_orders);
      return modutil::INVALID;
    }

    if(h.num_patterns > MAX_PATTERNS)
    {
      format::error("invalid pattern count %u > 256", h.num_patterns);
      return modutil::INVALID;
    }

    if(h.num_orders > h.header_size - 20)
    {
      format::error("header size %" PRIu32 " too small for %u orders", h.header_size, h.num_orders);
      return modutil::INVALID;
    }

    if(!fread(h.orders, h.num_orders, 1, fp))
      return modutil::READ_ERROR;

    // Skip any remaining header size.
    size_t skip = h.header_size - h.num_orders - 20;
    if(skip)
      if(fseek(fp, skip, SEEK_CUR))
        return modutil::SEEK_ERROR;

    for(size_t i = 0; i < h.num_orders; i++)
    {
      if(h.orders[i] >= h.num_patterns)
      {
        if(h.orders[i] == 0xFE && !invalid)
          mpt_extension = true;
        else
          invalid = true;
      }
      if(h.orders[i] == 0xFE && h.num_patterns > 0xFE)
        has_fe = true;
    }

    if(invalid)
    {
      m.uses[FT_ORDER_OVER_NUM_PATTERNS] = true;
    }
    else

    if(mpt_extension)
    {
      m.uses[FT_ORDER_FE_MODPLUG_SKIP] = true;
    }
    else

    if(has_fe)
      m.uses[FT_ORDER_FE] = true;


    /* Patterns and instruments. */
    modutil::error err;
    if(h.version >= 0x0104)
    {
      err = load_patterns(m, fp);
      if(err)
        return err;
      err = load_instruments(m, fp);
      if(err)
        return err;
    }
    else
    {
      err = load_instruments(m, fp);
      if(err)
        return err;
      err = load_patterns(m, fp);
      if(err)
        return err;
    }


    /* Print information. */

    format::line("Name",     "%s", m.name);
    format::line("Type",     "XM %04x %s", h.version, m.tracker);
    format::line("Instr.",   "%u", h.num_instruments);
    format::line("Samples",  "%zu", m.num_samples);
    format::line("Channels", "%u", h.num_channels);
    format::line("Patterns", "%u", h.num_patterns);
    format::line("Orders",   "%u", h.num_orders);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      // TODO print envelopes.
      static constexpr const char *i_labels[] =
      {
        "Name", "T", "#Smp", "Inst.HSz", "Smpl.HSz", "#VPt", "#PPt", "Fade", "VTp", "VSw", "VDe", "VRt"
      };

      static constexpr const char *s_labels[] =
      {
        "Name", "Ins", "Length", "LoopStart", "LoopLen", "Vol", "Fine", "Flg", "Tr"
      };

      table::table<
        table::string<22>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<6>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<4>,
        table::number<4>> i_table;

      table::table<
        table::string<22>,
        table::number<4, table::RIGHT|table::HEX>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<4>,
        table::number<4>> s_table;

      if(h.num_instruments)
      {
        format::line();
        i_table.header("Instr.", i_labels);

        for(size_t i = 0; i < h.num_instruments; i++)
        {
          XM_instrument &ins = m.instruments[i];
          i_table.row(i + 1, ins.name, {},
            ins.type, ins.num_samples, ins.header_size, ins.sample_header_size, {},
            ins.num_volume_points, ins.num_pan_points, ins.fadeout, {},
            ins.vibrato_type, ins.vibrato_sweep, ins.vibrato_depth, ins.vibrato_rate);
        }
      }

      if(m.num_samples)
      {
        format::line();
        s_table.header("Samples", s_labels);

        size_t smp = 1;
        for(size_t i = 0; i < h.num_instruments; i++)
        {
          XM_instrument &ins = m.instruments[i];
          for(size_t j = 0; j < ins.num_samples; j++)
          {
            XM_sample &s = ins.samples[j];
            s_table.row(smp, s.name, i, {},
              s.length, s.loop_start, s.loop_length, {},
              s.volume, s.finetune, s.type, s.transpose);
            smp++;
          }
        }
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
        XM_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::volume, format::effectXM>;
        format::pattern<EVENT> pattern(i, h.num_channels, p.num_rows, p.packed_size);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        XM_event *current = p.events;
        for(size_t row = 0; row < p.num_rows; row++)
        {
          for(size_t track = 0; track < h.num_channels; track++, current++)
          {
            format::note     a{ current->note };
            format::sample   b{ current->instrument };
            format::volume   c{ current->volume };
            format::effectXM d{ current->effect, current->param };

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
    if(!num_xms)
      return;

    format::report("Total XMs", num_xms);
  }
};

static const XM_loader loader;
