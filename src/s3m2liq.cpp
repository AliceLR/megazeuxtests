/**
 * Copyright (C) 2024 Lachesis <petrifiedrowan@gmail.com>
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

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#define NAME_STRING           "s3m2liq"
#define NAME_VERSION_STRING   NAME_STRING " 1.0.0"
#define AUTHOR_STRING         "IGNORED THE MESSAGE"
#define TRACKER_STRING        "LiquidTrackr1.30\xff"
#define LDSS_SOFTWARE_STRING  NAME_VERSION_STRING

#define ERROR(...) do { \
  fprintf(stderr, "ERROR: " __VA_ARGS__); \
  fprintf(stderr, "\n"); \
  fflush(stderr); \
} while(0)

static constexpr unsigned S3M_MAX_CHANNELS = 32;
static constexpr unsigned S3M_ROWS = 64;
static constexpr unsigned S3M_PATTERN_SIZE = S3M_MAX_CHANNELS * S3M_ROWS;

enum s3m_ffi
{
  S3M_SIGNED_SAMPLES = 1,
  S3M_UNSIGNED_SAMPLES = 2,
};

enum s3m_ins_flags
{
  S3M_LOOP = (1<<0),
  S3M_STEREO = (1<<1),
  S3M_16BIT = (1<<2),
};

enum s3m_effect
{
  S3M_NO_EFFECT,
  S3M_SPEED,
  S3M_JUMP,
  S3M_BREAK,
  S3M_VOLUME_SLIDE,
  S3M_PORTA_DN,
  S3M_PORTA_UP,
  S3M_TONEPORTA,
  S3M_VIBRATO,
  S3M_TREMOR,
  S3M_ARPEGGIO,
  S3M_VIBRATO_VOLSLIDE,
  S3M_TONEPORTA_VOLSLIDE,
  S3M_CHANNEL_VOLUME,     // MPT
  S3M_CHANNEL_VOLSLIDE,   // MPT
  S3M_OFFSET,
  S3M_PAN_SLIDE,          // MPT
  S3M_RETRIGGER,
  S3M_TREMOLO,
  S3M_EXTENDED,
  S3M_BPM,
  S3M_FINE_VIBRATO,
  S3M_GLOBAL_VOLUME,
  S3M_GLOBAL_VOLSLIDE,    // MPT
  S3M_PAN_CONTROL,
  S3M_PANBRELLO,          // MPT
  S3M_MIDI_MACRO,         // MPT
  MAX_S3M_EFFECT
};

enum s3m_extended
{
  S3M_S0_UNUSED,
  S3M_S1_GLISSANDO,
  S3M_S2_FINETUNE,
  S3M_S3_VIBRATO_WAVEFORM,
  S3M_S4_TREMOLO_WAVEFORM,
  S3M_S5_PANBRELLO_WAVEFORM,  // MPT
  S3M_S6_FINE_PATTERN_DELAY,
  S3M_S7_UNUSED,
  S3M_S8_PAN_CONTROL,
  S3M_S9_SOUND_CONTROL,       // MPT
  S3M_SA_HIGH_OFFSET,         // MPT
  S3M_SB_PATTERN_LOOP,
  S3M_SC_NOTE_CUT,
  S3M_SD_NOTE_DELAY,
  S3M_SE_PATTERN_DELAY,
  S3M_SF_UNUSED,
};

enum liq_flags
{
  LIQ_CUT_ON_LIMIT = (1<<0),
  LIQ_S3M_COMPATIBILITY = (1<<1),
};

enum ldss_flags
{
  LDSS_16BIT = (1<<0),
  LDSS_STEREO = (1<<1),
  LDSS_SIGNED = (1<<2),
};

enum liq_effect
{
  LIQ_ARPEGGIO            = 'A',
  LIQ_BPM                 = 'B',
  LIQ_CUT                 = 'C',
  LIQ_PORTA_DN            = 'D',
  LIQ_UNUSED_EXX          = 'E',
  LIQ_FINE_VIBRATO        = 'F',
  LIQ_GLOBAL_VOLUME       = 'G',
  LIQ_UNUSED_HXX          = 'H',
  LIQ_UNUSED_IXX          = 'I',
  LIQ_JUMP                = 'J',
  LIQ_UNUSED_KXX          = 'K',
  LIQ_VOLUME_SLIDE        = 'L',
  LIQ_EXTENDED            = 'M',
  LIQ_NOTEPORTA           = 'N',
  LIQ_OFFSET              = 'O',
  LIQ_PAN_CONTROL         = 'P',
  LIQ_UNUSED_QXX          = 'Q',
  LIQ_RETRIGGER           = 'R',
  LIQ_SPEED               = 'S',
  LIQ_TREMOLO             = 'T',
  LIQ_PORTA_UP            = 'U',
  LIQ_VIBRATO             = 'V',
  LIQ_UNUSED_WXX          = 'W',
  LIQ_TONEPORTA_VOLSLIDE  = 'X',
  LIQ_VIBRATO_VOLSLIDE    = 'Y',
  LIQ_NO_EFFECT           = 0xff
};

enum liq_extended
{
  LIQ_M0_UNUSED,
  LIQ_M1_UNUSED,
  LIQ_M2_UNUSED,
  LIQ_M3_GLISSANDO,
  LIQ_M4_VIBRATO_WAVEFORM,
  LIQ_M5_FINETUNE,
  LIQ_M6_PATTERN_LOOP,
  LIQ_M7_TREMOLO_WAVEFORM,
  LIQ_M8_UNUSED,
  LIQ_M9_UNUSED,
  LIQ_MA_UNUSED,
  LIQ_MB_UNUSED,
  LIQ_MC_NOTE_CUT,
  LIQ_MD_NOTE_DELAY,
  LIQ_ME_PATTERN_DELAY,
  LIQ_MF_UNUSED
};

#define EXTENDED(ex,param) ((((ex) & 0x0f) << 4) | ((param) & 0x0f))

struct event
{
  uint8_t note;
  uint8_t instrument;
  uint8_t volume;
  uint8_t effect;
  uint8_t param;

  bool operator==(const event &ev) const
  {
    return  note == ev.note &&
            instrument == ev.instrument &&
            volume == ev.volume &&
            effect == ev.effect &&
            param == ev.param;
  }

  bool operator!=(const event &ev) const
  {
    return !(*this == ev);
  }
};

struct s3m_instrument
{
  uint8_t   type; /* 0=unused, 1=sample, >=2 adlib */
  uint8_t   filename[12];
  unsigned  data_seg;    /* 24-bit mixed endian: hi, lo, mid */
  uint32_t  length;     /* sample only, sample frames */
  uint32_t  loopstart;  /* sample only, sample frames */
  uint32_t  loopend;    /* sample only, sample frames */
  uint8_t   default_volume;
  uint8_t   dsk;        /* adlib */
  uint8_t   packing;    /* sample only; Mod Plugin uses 4 for ADPCM */
  uint8_t   flags;      /* 1=loop 2=stereo 4=16bit */
  uint32_t  rate;
  uint32_t  reserved;
  uint16_t  int_gp;     /* sample only, address in Gravis RAM */
  uint16_t  int_512;    /* sample only, flags for Sound Blaster looping */
  uint32_t  int_lastpos;/* sample only, last position for Sound Blaster */
  uint8_t   name[28];
  uint8_t   magic[4];   /* SCRS */
};

struct s3m_header
{
  uint8_t   name[28];
  uint8_t   eof;
  uint8_t   type;
  uint16_t  reserved;
  uint16_t  num_orders;
  uint16_t  num_instruments;
  uint16_t  num_patterns;
  uint16_t  flags;
  uint16_t  cwtv;
  uint16_t  ffi;
  uint8_t   magic[4];   /* SCRM */
  uint8_t   global_volume;
  uint8_t   initial_speed;
  uint8_t   initial_bpm;
  uint8_t   mix_volume;
  uint8_t   click_removal;
  uint8_t   has_panning_table; /* 252=panning table present */
  uint8_t   reserved2[8];
  uint16_t  special_seg;
  uint8_t   channel_settings[S3M_MAX_CHANNELS];

  uint8_t   order[256];
  uint16_t  instrument_seg[256];
  uint16_t  pattern_seg[256];
  uint8_t   channel_pan[S3M_MAX_CHANNELS];

  /* Derived */
  size_t    num_channels;
};

struct ldss
{
  uint8_t   magic[4]; /* LDSS */
  uint16_t  version; /* 0x101 */
  uint8_t   name[30];
  uint8_t   software[20];
  uint8_t   author[20];
  uint8_t   sound_board; /* 255=unknown */
  uint32_t  length; /* bytes */
  uint32_t  loopstart; /* bytes */
  uint32_t  loopend; /* bytes */
  uint32_t  rate; /* Hz */
  uint8_t   default_volume;
  uint8_t   flags; /* 1=16bit 2=stereo */
  uint8_t   default_pan; /* 32=center, 255=no default pan */
  uint8_t   midi_patch; /* 255=undefined */
  uint8_t   global_volume; /* 32=default, 64=2x gain */
  uint8_t   chord_type; /* 255=undefined */
  uint16_t  header_bytes; /* 0x90 */
  uint16_t  compression; /* 0 */
  uint32_t  crc32; /* 0=ignore */
  uint8_t   midi_channel; /* 255=undefined */
  int8_t    loop_type; /* -1 or 0=normal, 1=ping pong */
  uint8_t   reserved[10];
  uint8_t   filename[25];
};

struct liq_pattern
{
  uint8_t   magic[4]; /* LP\0\0 */
  uint8_t   name[30];
  uint16_t  num_rows;
  uint32_t  packed_size;
  uint32_t  reserved;
};

struct liq_header
{
  uint8_t   magic[14]; /* Liquid Module: */
  uint8_t   name[30];
  uint8_t   author[20];
  uint8_t   eof; /* 0x1a */
  uint8_t   tracker[20];
  uint16_t  format_version; /* 0.00, 1.00, 1.01, or allegedly 1.02 */
  uint16_t  initial_speed;
  uint16_t  initial_bpm;
  uint16_t  lowest_note;  /* Amiga period; usually 6848 = C-0 */
  uint16_t  highest_note; /* Amiga period;
                           * imported NO and 0.00 are 128 = A-5;
                           * all 1.00 are 112=B-5 or 28=B-7 */
  uint16_t  num_channels;
  uint32_t  flags;        /* 1 = cut upon limit (porta out of range cuts note)
                           * 2 = ST3 compatibility mode (not well-defined) */
  uint16_t  num_patterns;
  uint16_t  num_instruments;
  uint16_t  num_orders;   /* module header size in 0.00 */
  uint16_t  header_size;  /* includes initial pan/volume, sequence, echo pools, etc */
  /* 0x6D */

  uint8_t   initial_volume[256];
  uint8_t   initial_pan[256];
  uint8_t   order[256];
};

static uint16_t read_u16le(const uint8_t *data)
{
  return data[0] | (data[1] << 8);
}

static uint32_t read_u32le(const uint8_t *data)
{
  return data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24u);
}

static void write_u16le(uint8_t *data, uint16_t value)
{
  data[0] = value & 0xff;
  data[1] = value >> 8;
}

static void write_u32le(uint8_t *data, uint32_t value)
{
  data[0] = value & 0xff;
  data[1] = value >> 8;
  data[2] = value >> 16;
  data[3] = value >> 24;
}

template<int N>
static size_t s3m_strlen(const uint8_t (&buf)[N])
{
  size_t len;
  for(len = 0; len < N; len++)
    if(buf[len] == '\0')
      break;
  for(; len > 0; len--)
    if(buf[len] != ' ')
      break;
  return len;
}


/** Load S3M */

static bool load_s3m_header(s3m_header &s3m, FILE *in)
{
  uint8_t buf[512];
  size_t i;

  if(fread(buf, 1, 96, in) < 96)
  {
    ERROR("read error on input");
    return false;
  }
  if(memcmp(buf + 44, "SCRM", 4))
  {
    ERROR("not an S3M");
    return false;
  }

  memcpy(s3m.name, buf, 28);
  s3m.eof = buf[28];
  s3m.type = buf[29];
  s3m.reserved = read_u16le(buf + 30);
  s3m.num_orders = read_u16le(buf + 32);
  s3m.num_instruments = read_u16le(buf + 34);
  s3m.num_patterns = read_u16le(buf + 36);
  s3m.flags = read_u16le(buf + 38);
  s3m.cwtv = read_u16le(buf + 40);
  s3m.ffi = read_u16le(buf + 42);
  s3m.global_volume = buf[48];
  s3m.initial_speed = buf[49];
  s3m.initial_bpm = buf[50];
  s3m.mix_volume = buf[51];
  s3m.click_removal = buf[52];
  s3m.has_panning_table = buf[53];
  memcpy(s3m.reserved2, buf + 54, 8);
  s3m.special_seg = read_u16le(buf + 62);
  memcpy(s3m.channel_settings, buf + 64, 32);

  if(s3m.num_orders > 256)
  {
    ERROR("S3M length >256");
    return false;
  }
  if(s3m.num_instruments > 99)
  {
    ERROR("S3M has >99 instruments");
    return false;
  }
  if(s3m.num_patterns > 256)
  {
    ERROR("S3M has >256 patterns");
    return false;
  }

  /* Order table */
  if(fread(s3m.order, 1, s3m.num_orders, in) < s3m.num_orders)
  {
    ERROR("read error on input (orders)");
    return false;
  }

  /* Instrument parapointers */
  if(fread(buf, 2, s3m.num_instruments, in) < s3m.num_instruments)
  {
    ERROR("read error on input (instrument pp)");
    return false;
  }
  for(i = 0; i < s3m.num_instruments; i++)
    s3m.instrument_seg[i] = read_u16le(buf + i * 2);

  /* Pattern parapointers */
  if(fread(buf, 2, s3m.num_patterns, in) < s3m.num_patterns)
  {
    ERROR("read error on input (pattern pp)");
    return false;
  }
  for(i = 0; i < s3m.num_patterns; i++)
    s3m.pattern_seg[i] = read_u16le(buf + i * 2);

  /* Panning table */
  if(s3m.has_panning_table == 252)
  {
    if(fread(s3m.channel_pan, 1, 32, in) < 32)
    {
      ERROR("read error on input (pan table)");
      return false;
    }
  }

  /* Calculate real number of channels. Do not bother to reorder left/right
   * channels; this converter is more interested in adapting structure 1-to-1
   * rather than accuracy. */
  s3m.num_channels = 0;
  for(i = 0; i < S3M_MAX_CHANNELS; i++)
    if(s3m.channel_settings[i] < 16)
      s3m.num_channels = i + 1;

  return true;
}

static bool seek_seg(unsigned seg, FILE *in)
{
  if(fseek(in, (long)seg << 4L, SEEK_SET) < 0)
    return false;

  return true;
}

static bool load_s3m_pattern(std::vector<event> &events,
 std::vector<uint8_t> &data, unsigned seg, FILE *in)
{
  if(!seg)
  {
    data.resize(0);
    return true;
  }
  if(!seek_seg(seg, in))
  {
    ERROR("seek error on input");
    return false;
  }

  memset(events.data(), 0, events.size() * sizeof(event));

  uint16_t packed_size = fgetc(in) | (fgetc(in) << 8);
  data.resize(packed_size);
  if(!packed_size)
    return true;

  if(fread(data.data(), 1, packed_size, in) < packed_size)
  {
    ERROR("read error on input (seg %u)", seg);
    return false;
  }

  unsigned pos = 0;
  unsigned row = 0;
  while(pos < packed_size && row < S3M_ROWS)
  {
    uint8_t flg = data[pos++];
    if(!flg)
    {
      row++;
      continue;
    }

    uint8_t chn = flg & 0x1f;
    /* Reorder to LIQ track-major, row-minor style, */
    event &ev = events[chn * S3M_ROWS + row];

    if(flg & 0x20)
    {
      if(pos + 2 > packed_size)
      {
        ERROR("packing error");
        return false;
      }
      ev.note = data[pos++];
      ev.instrument = data[pos++];
    }

    if(flg & 0x40)
    {
      if(pos >= packed_size)
      {
        ERROR("packing error");
        return false;
      }
      ev.volume = data[pos++] + 1;
    }

    if(flg & 0x80)
    {
      if(pos + 2 > packed_size)
      {
        ERROR("packing error");
        return false;
      }
      ev.effect = data[pos++];
      ev.param = data[pos++];
    }
  }
  return true;
}

static bool load_s3m_instrument(s3m_instrument &ins, std::vector<uint8_t> &data,
 unsigned seg, FILE *in)
{
  uint8_t buf[80];

  if(!seg)
    return true;

  if(!seek_seg(seg, in))
  {
    ERROR("seek error on input");
    return false;
  }

  if(fread(buf, 1, 80, in) < 80)
  {
    ERROR("read error on input");
    return false;
  }

  ins.type = buf[0];
  if(ins.type >= 2)
  {
    ERROR("unsupported adlib instrument");
    return false;
  }

  memcpy(ins.filename, buf + 1, 12);
  ins.data_seg = read_u16le(buf + 14) | (buf[13] << 16);
  ins.length = read_u32le(buf + 16);
  ins.loopstart = read_u32le(buf + 20);
  ins.loopend = read_u32le(buf + 24);
  ins.default_volume = buf[28];
  ins.dsk = buf[29];
  ins.packing = buf[30];
  ins.flags = buf[31];
  ins.rate = read_u32le(buf + 32);
  ins.reserved = read_u32le(buf + 36);
  ins.int_gp = read_u16le(buf + 40);
  ins.int_512 = read_u16le(buf + 42);
  ins.int_lastpos = read_u32le(buf + 44);
  memcpy(ins.name, buf + 48, 28);

  if(ins.type != 1 || !ins.data_seg)
  {
    ins.length = 0;
    data.resize(0);
    return true;
  }

  size_t real_length = ins.length;
  if(ins.flags & S3M_16BIT)
    real_length <<= 1;
  if(ins.flags & S3M_STEREO)
    real_length <<= 1;

  if(!seek_seg(ins.data_seg, in))
  {
    ERROR("seek error on input (sample data) (seg %u)", ins.data_seg);
    return false;
  }

  data.resize(real_length);
  if(fread(data.data(), 1, real_length, in) < real_length)
  {
    ERROR("read error on input (sample data) (seg %u)", ins.data_seg);
    return false;
  }
  return true;
}


/** Conversion */

static bool convert_s3m_header(liq_header &liq, const s3m_header &s3m)
{
  size_t i;

  memcpy(liq.magic, "Liquid Module:", 14);
  memset(liq.name, ' ', sizeof(liq.name));
  memcpy(liq.name, s3m.name, s3m_strlen(s3m.name));
  memset(liq.author, ' ', sizeof(liq.author));
  memcpy(liq.author, AUTHOR_STRING, strlen(AUTHOR_STRING));
  liq.eof = 0x1a;
  memset(liq.tracker, ' ', sizeof(liq.tracker));
  memcpy(liq.tracker, TRACKER_STRING, strlen(TRACKER_STRING));

  liq.format_version  = 0x100;
  liq.initial_speed   = s3m.initial_speed;
  liq.initial_bpm     = s3m.initial_bpm;
  liq.lowest_note     = 6848;
  liq.highest_note    = 28;
  liq.num_channels    = s3m.num_channels;
  liq.flags           = LIQ_S3M_COMPATIBILITY; /* TODO: configurable */
  liq.num_patterns    = s3m.num_patterns;
  liq.num_instruments = s3m.num_instruments;
  liq.num_orders      = 0;
  liq.header_size     = 0;

  bool is_mono = !(s3m.mix_volume & 0x80);
  for(i = 0; i < liq.num_channels; i++)
  {
    /* Initial pan */
    if(s3m.channel_pan[i] & 0x20)
    {
      liq.initial_pan[i] = (s3m.channel_pan[i] & 0x0f) * 64 / 15;
    }
    else

    if(!is_mono)
    {
      liq.initial_pan[i] = (s3m.channel_settings[i] < 8) ? 16 : 48;
    }
    else
      liq.initial_pan[i] = 32;

    /* Initial volume */
    if(s3m.channel_settings[i] < 16)
      liq.initial_volume[i] = 0x20;
    else
      liq.initial_volume[i] = 0;
  }

  size_t j = 0;
  for(i = 0; i < s3m.num_orders; i++)
  {
    if(s3m.order[i] == 255)
      break;
    if(s3m.order[i] < 254)
      liq.order[j++] = s3m.order[i];
  }
  liq.num_orders  = j;
  liq.header_size = 0x6d + (liq.num_channels * 2) + (liq.num_orders);
  return true;
}

static void convert_s3m_event(event &event)
{
  /* 254=key off
   * 255=empty note
   * otherwise: hi=octave, lo=note
   * S3M octave 4 -> LIQ octave 2 */
  if(event.note < 0x20)
    event.note = 255;
  else
  if(event.note < 254)
    event.note = ((event.note >> 4) - 2) * 12 + (event.note & 0x0f);

  /* 00=.. */
  event.instrument--;

  /* 255=.. */
  event.volume = event.volume <= 65 ? event.volume - 1 : 0xff;

  /* Convert effect. Note that some effects (especially retrigger)
   * are not 1-to-1 conversions, but this converter doesn't care about
   * S3M compatibility.
   */
  static constexpr const uint8_t fx[MAX_S3M_EFFECT] =
  {
    LIQ_NO_EFFECT,
    LIQ_SPEED,
    LIQ_JUMP,
    LIQ_CUT,
    LIQ_VOLUME_SLIDE,
    LIQ_PORTA_DN,
    LIQ_PORTA_UP,
    LIQ_NOTEPORTA,
    LIQ_VIBRATO,
    LIQ_NO_EFFECT,          /* Tremor */
    LIQ_ARPEGGIO,
    LIQ_VIBRATO_VOLSLIDE,
    LIQ_TONEPORTA_VOLSLIDE,
    LIQ_NO_EFFECT,          /* Channel volume */
    LIQ_NO_EFFECT,          /* Channel volume slide */
    LIQ_OFFSET,
    LIQ_NO_EFFECT,          /* Pan slide */
    LIQ_RETRIGGER,
    LIQ_TREMOLO,
    LIQ_EXTENDED,
    LIQ_BPM,
    LIQ_FINE_VIBRATO,
    LIQ_GLOBAL_VOLUME,
    LIQ_NO_EFFECT,          /* Global volume slide */
    LIQ_PAN_CONTROL,
    LIQ_NO_EFFECT,          /* Panbrello */
    LIQ_NO_EFFECT,          /* Midi macro */
  };
  static constexpr const uint8_t efx[16] =
  {
    LIQ_NO_EFFECT,          /* Unused */
    LIQ_M3_GLISSANDO,
    LIQ_M5_FINETUNE,
    LIQ_M4_VIBRATO_WAVEFORM,
    LIQ_M7_TREMOLO_WAVEFORM,
    LIQ_NO_EFFECT,          /* Panbrello waveform */
    LIQ_NO_EFFECT,          /* Fine pattern delay */
    LIQ_NO_EFFECT,          /* Unused */
    LIQ_NO_EFFECT,          /* Pan control (special handling) */
    LIQ_NO_EFFECT,          /* Sound control (special handling) */
    LIQ_NO_EFFECT,          /* High offset */
    LIQ_M6_PATTERN_LOOP,
    LIQ_MC_NOTE_CUT,
    LIQ_MD_NOTE_DELAY,
    LIQ_ME_PATTERN_DELAY,
    LIQ_NO_EFFECT,          /* Unused */
  };
  if(event.effect < MAX_S3M_EFFECT)
    event.effect = fx[event.effect];
  else
    event.effect = LIQ_NO_EFFECT;

  switch(event.effect)
  {
  case LIQ_NO_EFFECT:
    event.param = LIQ_NO_EFFECT;
    break;

  case LIQ_GLOBAL_VOLUME:
    event.param = (event.param / 10 * 16) + (event.param % 10);
    break;

  case LIQ_PAN_CONTROL:
    event.param = event.param * 64 / 128;
    event.param = (event.param / 10 * 16) + (event.param % 10);
    break;

  case LIQ_EXTENDED:
    switch(event.param >> 4)
    {
      uint8_t tmp;
    default:
      tmp = efx[event.param >> 4];
      if(tmp >= 16)
      {
        event.effect = LIQ_NO_EFFECT;
        event.param = LIQ_NO_EFFECT;
      }
      else
        event.param = (tmp << 4) | (event.param & 0x0f);
      break;

    case S3M_S8_PAN_CONTROL:
      event.effect = LIQ_PAN_CONTROL;
      event.param = (event.param & 0x0f) * 64 / 15;
      event.param = (event.param / 10 * 16) + (event.param % 10);
      break;

    case S3M_S9_SOUND_CONTROL:
      if(event.param == 0x91)
      {
        event.effect = LIQ_PAN_CONTROL;
        event.param = 0x66;
      }
      else
      {
        event.effect = LIQ_NO_EFFECT;
        event.param = LIQ_NO_EFFECT;
      }
      break;
    }
  }
}

static uint8_t event_mask(const event &event)
{
  uint8_t mask = 0;

  if(event.note != 0xff)
    mask |= 0x01;
  if(event.instrument != 0xff)
    mask |= 0x02;
  if(event.volume != 0xff)
    mask |= 0x04;
  if(event.effect != 0xff)
    mask |= 0x08;
  if(event.param != 0xff)
    mask |= 0x10;

  return mask;
}

static void pack_event(std::vector<uint8_t> &data, const event &event, uint8_t mask)
{
  if(mask & 0x01)
    data.push_back(event.note);
  if(mask & 0x02)
    data.push_back(event.instrument);
  if(mask & 0x04)
    data.push_back(event.volume);
  if(mask & 0x08)
    data.push_back(event.effect);
  if(mask & 0x10)
    data.push_back(event.param);
}

static bool convert_s3m_pattern(const liq_header &liq, liq_pattern &lp,
 std::vector<event> &events, std::vector<uint8_t> &data)
{
  bool empty = data.size() == 0;

  memset(lp.name, ' ', sizeof(lp.name));
  lp.num_rows = S3M_ROWS;
  lp.packed_size = 0;
  lp.reserved = 0;

  /* Documentation claims !!!! for empty patterns, similar to ???? for
   * empty instruments, but Liquid Tracker 1.50 ignores the magic and
   * expects a full pattern definition to follow anyway. */
  memcpy(lp.magic, "LP\0\0", 4);

  for(event &event : events)
    convert_s3m_event(event);

  data.resize(0);
  data.reserve(1 << 16);

  size_t total = empty ? 0 : liq.num_channels * S3M_ROWS;
  for(size_t pos = 0; pos < total; )
  {
    size_t next_track = (pos / S3M_ROWS + 1) * S3M_ROWS;
    const event &event = events[pos++];
    uint8_t mask = event_mask(event);

    bool identical = true;
    size_t count = 1;
    size_t end_pos;
    // FIXME: identical not optimal here
    for(end_pos = pos; end_pos < next_track; end_pos++)
    {
      if(event_mask(events[end_pos]) == mask)
      {
        count++;
        if(event != events[end_pos])
          identical = false;
      }
      else
        break;
    }

    /* Repeated events. Don't allow a full event here; it's more
     * efficient to emit full events without any packing. */
    if(count > 1 && mask != 0x1f)
    {
      if(mask == 0x00)
      {
        size_t num_tracks = 1;
        if(end_pos == next_track && next_track < total)
        {
          // FIXME: are the next several tracks also skippable?
        }

        if(end_pos == next_track)
        {
          if(num_tracks > 1)
          {
            /* Skip multiple tracks */
            data.push_back(0xe1);
            data.push_back(num_tracks - 1);
            pos = next_track + S3M_ROWS * (num_tracks - 1);
          }
          else
          {
            /* Skip rest of current track */
            data.push_back(0xa0);
            pos = end_pos;
          }
        }
        else
        {
          /* No event, repeated */
          data.push_back(0xe0);
          data.push_back(count - 1);
          pos = end_pos;
        }
      }
      else

      if(identical)
      {
        /* Packed event, repeated */
        data.push_back(0x80 | mask);
        data.push_back(count - 1);
        pack_event(data, event, mask);
        pos = end_pos;
      }
      else
      {
        /* Packed events with same mask, repeated */
        data.push_back(0xa0 | mask);
        data.push_back(count - 1);
        pack_event(data, event, mask);
        for(size_t i = 1; i < count; i++)
          pack_event(data, events[pos++], mask);
      }
    }
    else

    if(mask == 0x00)
    {
      /* No event, one-off */
      data.push_back(0x80);
    }
    else

    if(mask != 0x1f)
    {
      /* Packed event, one-off */
      data.push_back(0xc0 | mask);
      pack_event(data, event, mask);
    }
    else
    {
      /* Unpacked event, one-off */
      data.push_back(event.note);
      data.push_back(event.instrument);
      data.push_back(event.volume);
      data.push_back(event.effect);
      data.push_back(event.param);
    }
  }
  data.push_back(0xc0);
  lp.packed_size = data.size();
  return true;
}

static bool convert_s3m_instrument(ldss &ls, const s3m_header &s3m,
 const s3m_instrument &ins, std::vector<uint8_t> &data)
{
  memcpy(ls.magic, "LDSS", 4);
  ls.version = 0x101;
  memset(ls.name, ' ', sizeof(ls.name));
  memcpy(ls.name, ins.name, s3m_strlen(ins.name));
  memset(ls.software, ' ', sizeof(ls.software));
  memcpy(ls.software, LDSS_SOFTWARE_STRING, strlen(LDSS_SOFTWARE_STRING));
  memset(ls.author, ' ', sizeof(ls.author));
  memcpy(ls.author, AUTHOR_STRING, strlen(AUTHOR_STRING));
  ls.sound_board    = 0xff;
  ls.length         = ins.length;
  ls.loopstart      = ins.loopstart;
  ls.loopend        = (ins.flags & S3M_LOOP) ? ins.loopend : 0;
  ls.rate           = ins.rate;
  ls.default_volume = ins.default_volume;
  ls.flags          = 0;
  ls.default_pan    = 0xff;
  ls.midi_patch     = 0xff;
  ls.global_volume  = 32;
  ls.chord_type     = 0xff;
  ls.header_bytes   = 0x90;
  ls.compression    = 0;
  ls.crc32          = 0;
  ls.midi_channel   = 0xff;
  ls.loop_type      = 0; // TODO: hack on bidi
  memset(ls.reserved, 0, sizeof(ls.reserved));
  memset(ls.filename, ' ', sizeof(ls.filename));
  memcpy(ls.filename, ins.filename, s3m_strlen(ins.filename));

  ls.flags |= LDSS_SIGNED;
  if(ins.flags & S3M_16BIT)
  {
    ls.flags |= LDSS_16BIT;
    ls.length <<= 1;
    ls.loopstart <<= 1;
    ls.loopend <<= 1;
  }
  if(ins.flags & S3M_STEREO)
  {
    ls.flags |= LDSS_STEREO;
    ls.length <<= 1;
    ls.loopstart <<= 1;
    ls.loopend <<= 1;
  }

  /* LDSS unsigned is completely ignored; unsigned samples need to be
   * converted to signed here. */
  if(s3m.ffi != S3M_SIGNED_SAMPLES)
  {
    if(ins.flags & S3M_16BIT)
    {
      uint16_t *w = reinterpret_cast<uint16_t *>(data.data());
      for(size_t i = 0; i < data.size(); i += 2)
        *(w++) ^= 0x8000;
    }
    else
    {
      for(size_t i = 0; i < data.size(); i++)
        data[i] ^= 0x80;
    }
  }
  return true;
}


/** Output LIQ */

static bool write_liq_header(const liq_header &liq, FILE *out)
{
  uint8_t buf[0x6d];
  memcpy(buf + 0, liq.magic, 14);
  memcpy(buf + 14, liq.name, 30);
  memcpy(buf + 44, liq.author, 20);
  buf[64] = liq.eof;
  memcpy(buf + 65, liq.tracker, 20);
  write_u16le(buf + 85, liq.format_version);
  write_u16le(buf + 87, liq.initial_speed);
  write_u16le(buf + 89, liq.initial_bpm);
  write_u16le(buf + 91, liq.lowest_note);
  write_u16le(buf + 93, liq.highest_note);
  write_u16le(buf + 95, liq.num_channels);
  write_u32le(buf + 97, liq.flags);
  write_u16le(buf + 101, liq.num_patterns);
  write_u16le(buf + 103, liq.num_instruments);
  write_u16le(buf + 105, liq.num_orders);
  write_u16le(buf + 107, liq.header_size);

  if(fwrite(buf, 1, sizeof(buf), out) < sizeof(buf))
  {
    ERROR("write error on output");
    return false;
  }
  if(fwrite(liq.initial_pan, 1, liq.num_channels, out) < liq.num_channels)
  {
    ERROR("write error on output (initial pan)");
    return false;
  }
  if(fwrite(liq.initial_volume, 1, liq.num_channels, out) < liq.num_channels)
  {
    ERROR("write error on output (initial volume)");
    return false;
  }
  if(fwrite(liq.order, 1, liq.num_orders, out) < liq.num_orders)
  {
    ERROR("write error on output (sequence)");
    return false;
  }
  if(ftell(out) != (long)liq.header_size)
  {
    ERROR("internal error: pos is %ld but should be %u", ftell(out), liq.header_size);
    return false;
  }
  return true;
}

static bool write_liq_pattern(const liq_pattern &lp,
 const std::vector<uint8_t> &data, FILE *out)
{
  if(!memcmp(lp.magic, "!!!!", 4))
  {
    fputs("!!!!", out);
    return true;
  }

  uint8_t buf[44];
  memcpy(buf + 0, lp.magic, 4);

  memcpy(buf + 4, lp.name, 30);
  write_u16le(buf + 34, lp.num_rows);
  write_u32le(buf + 36, lp.packed_size);
  write_u32le(buf + 40, lp.reserved);

  if(fwrite(buf, 1, sizeof(buf), out) < sizeof(buf))
  {
    ERROR("write error on output");
    return false;
  }
  if(fwrite(data.data(), 1, data.size(), out) < data.size())
  {
    ERROR("write error on output (pattern data)");
    return false;
  }
  return true;
}

static bool write_liq_instrument(const ldss &ls,
 const std::vector<uint8_t> &data, FILE *out)
{
  if(!memcmp(ls.magic, "????", 4))
  {
    fputs("????", out);
    return true;
  }

  uint8_t buf[0x90];
  memcpy(buf + 0, ls.magic, 4);
  write_u16le(buf + 4, ls.version);
  memcpy(buf + 6, ls.name, 30);
  memcpy(buf + 36, ls.software, 20);
  memcpy(buf + 56, ls.author, 20);
  buf[76] = ls.sound_board;
  write_u32le(buf + 77, ls.length);
  write_u32le(buf + 81, ls.loopstart);
  write_u32le(buf + 85, ls.loopend);
  write_u32le(buf + 89, ls.rate);
  buf[93] = ls.default_volume;
  buf[94] = ls.flags;
  buf[95] = ls.default_pan;
  buf[96] = ls.midi_patch;
  buf[97] = ls.global_volume;
  buf[98] = ls.chord_type;
  write_u16le(buf + 99, ls.header_bytes);
  write_u16le(buf + 101, ls.compression);
  write_u32le(buf + 103, ls.crc32);
  buf[107] = ls.midi_channel;
  buf[108] = ls.loop_type;
  memcpy(buf + 109, ls.reserved, 10);
  memcpy(buf + 119, ls.filename, 25);

  if(fwrite(buf, 1, sizeof(buf), out) < sizeof(buf))
  {
    ERROR("write error on output");
    return false;
  }
  if(fwrite(data.data(), 1, data.size(), out) < data.size())
  {
    ERROR("write error on output (sample data)");
    return false;
  }
  return true;
}


/** Main */

int main(int argc, char *argv[])
{
  std::vector<event> events;
  std::vector<uint8_t> data;
  char path[1024];

  fprintf(stderr,
    NAME_VERSION_STRING "\n"
    "Copyright (C) 2024 Lachesis\n"
    "\n"
    "NOTICE: This utility is intended for replayer research for Liquid\n"
    "Tracker 0.80b+ ONLY. This utility makes NO ATTEMPT to ensure accurate\n"
    "S3M conversion, and in fact intentionally avoids it in some cases\n"
    "(such as channel execution order) for convenience. Any S3M provided\n"
    "to this utility should have been crafted WITH THE EXPRESS PURPOSE of\n"
    "being interpreted as a Liquid Tracker .LIQ, and the output file should\n"
    "be well-tested with Liquid Tracker. To encourage the acknowledgement\n"
    "of this, s3m2liq will inject '" AUTHOR_STRING "' in all author fields; you are\n"
    "encouraged to correct output files with a hex editor or edit the source to\n"
    "adjust this. To replayer authors: if you attempt to detect files made\n"
    "with this tool, they should be played as if they are original Liquid\n"
    "Tracker modules, not as if they are S3Ms.\n"
    "\n"
    "DO NOT USE THIS UTILITY FOR STUPID CRAP!\n"
    "\n"
  );
  if(argc < 2)
  {
    fprintf(stderr, "Usage: " NAME_STRING " file.s3m [...]\n"
                    "Writes LIQ 1.00 conversion of [name].s3m to [name].liq.\n");
    return 1;
  }

  events.resize(S3M_PATTERN_SIZE);

  for(int i = 1; i < argc; i++)
  {
    FILE *in;
    FILE *out;
    char *extpos;
    size_t len;

    fprintf(stderr, "  %s... ", argv[i]);
    fflush(stderr);

    in = fopen(argv[i], "rb");
    if(!in)
    {
      ERROR("failed to fopen '%s'", argv[i]);
      continue;
    }

    s3m_header s3m{};
    liq_header liq{};
    if(!load_s3m_header(s3m, in) || !convert_s3m_header(liq, s3m))
    {
      ERROR("failed to convert '%s'", argv[i]);
      goto err_close;
    }

    len = strlen(argv[i]);
    extpos = strrchr(argv[i], '.');
    if(len + 5 > sizeof(path))
    {
      ERROR("path too long, skipping '%s'", argv[i]);
      goto err_close;
    }
    if(extpos && !strcasecmp(extpos, ".mod"))
    {
      snprintf(path, sizeof(path), "%.*s.liq",
       (int)(extpos - argv[i]), argv[i]);
    }
    else
      snprintf(path, sizeof(path), "%s.liq", argv[i]);

    out = fopen(path, "wb");
    if(!out)
    {
      ERROR("failed to fopen '%s' output file '%s'", argv[i], path);
      goto err_close;
    }
    if(!write_liq_header(liq, out))
    {
      ERROR("failed to convert '%s'", argv[i]);
      goto err_close2;
    }

    // Convert and copy patterns
    for(size_t j = 0; j < s3m.num_patterns; j++)
    {
      liq_pattern lp{};

      if(!load_s3m_pattern(events, data, s3m.pattern_seg[j], in) ||
         !convert_s3m_pattern(liq, lp, events, data) ||
         !write_liq_pattern(lp, data, out))
      {
        ERROR("failed to convert '%s' pattern %zu", argv[i], j);
        goto err_close2;
      }
    }

    // Convert and copy instruments
    for(size_t k = 0; k < s3m.num_instruments; k++)
    {
      s3m_instrument ins{};
      ldss ls{};

      if(!load_s3m_instrument(ins, data, s3m.instrument_seg[k], in) ||
         !convert_s3m_instrument(ls, s3m, ins, data) ||
         !write_liq_instrument(ls, data, out))
      {
        ERROR("failed to convert '%s' instrument %zu", argv[i], k);
        goto err_close2;
      }
    }
    fprintf(stderr, "OK\n");
    fflush(stderr);

err_close2:
    fclose(out);
err_close:
    fclose(in);
  }
  return 0;
}
