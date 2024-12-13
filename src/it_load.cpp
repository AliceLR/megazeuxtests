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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Bitstream.hpp"
#include "modutil.hpp"

static int num_its;
//static int num_it_instrument_mode;
//static int num_it_sample_gvol;
//static int num_it_sample_vibrato;


enum IT_features
{
  FT_OLD_FORMAT,
  FT_MIDI_CONFIG,
  FT_SAMPLE_MODE,
  FT_INSTRUMENT_MODE,
  FT_SAMPLE_GLOBAL_VOLUME,
  FT_SAMPLE_VIBRATO,
  FT_SAMPLE_COMPRESSION,
  FT_SAMPLE_COMPRESSION_1_4TH,
  FT_SAMPLE_COMPRESSION_1_8TH,
  FT_SAMPLE_COMPRESSION_INVALID_WIDTH,
  FT_SAMPLE_STEREO,
  FT_SAMPLE_16,
  FT_SAMPLE_ADPCM,
  FT_ENV_VOLUME,
  FT_ENV_PAN,
  FT_ENV_PITCH,
  FT_ENV_FILTER,
  FT_E_MACROSET,
  FT_E_MACRO,
  FT_E_MACROSMOOTH,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "<2.00",
  "MidiCfg",
  "SmplMode",
  "InstMode",
  "SmpGVL",
  "SmpVib",
  "SmpCmp",
  "SmpCmp<1/4th",
  "SmpCmp<1/8th",
  "SmpCmpInvalidBW",
  "S:Stereo",
  "S:16",
  "S:ADPCM",
  "EnvVol",
  "EnvPan",
  "EnvPitch",
  "EnvFilter",
  "E:MacroSet",
  "E:Macro",
  "E:MacroSmooth",
};

enum IT_flags
{
  F_STEREO           = (1 << 0),
  F_VOL_0_OPT        = (1 << 1),
  F_INST_MODE        = (1 << 2),
  F_LINEAR_PORTA     = (1 << 3),
  F_OLD_EFFECTS      = (1 << 4),
  F_SHARED_PORTA_MEM = (1 << 5),
  F_MIDI_PITCH       = (1 << 6),
  F_MIDI_CONFIG      = (1 << 7)
};

enum IT_special
{
  FS_SONG_MESSAGE    = (1 << 0),
  FS_MIDI_CONFIG     = (1 << 3)
};

enum IT_sample_flags
{
  SAMPLE_SET               = (1 << 0),
  SAMPLE_16_BIT            = (1 << 1),
  SAMPLE_STEREO            = (1 << 2),
  SAMPLE_COMPRESSED        = (1 << 3),
  SAMPLE_LOOP              = (1 << 4),
  SAMPLE_SUSTAIN_LOOP      = (1 << 5),
  SAMPLE_BIDI_LOOP         = (1 << 6),
  SAMPLE_BIDI_SUSTAIN_LOOP = (1 << 7),
};

enum IT_vibrato_waveforms
{
  WF_SINE_WAVE,
  WF_RAMP_DOWN,
  WF_SQUARE_WAVE,
  WF_RANDOM
};

static constexpr int MAX_ENVELOPE = 25;

static const char *NNA_string(unsigned int nna)
{
  static const char NNA_TYPE[4][5]
  {
    "Cut", "Cont", "Off", "Fade"
  };
  return (nna < (size_t)arraysize(NNA_TYPE)) ? NNA_TYPE[nna] : "?";
}

static const char *DCT_string(unsigned int dct)
{
  static const char DCT_TYPE[4][5]
  {
    "Off", "Note", "Smpl", "Inst"
  };
  return (dct < (size_t)arraysize(DCT_TYPE)) ? DCT_TYPE[dct] : "?";
}

static const char *DCA_string(unsigned int dca)
{
  static const char DCA_TYPE[3][5]
  {
    "Cut", "Off", "Fade"
  };
  return (dca < (size_t)arraysize(DCA_TYPE)) ? DCA_TYPE[dca] : "?";
}


struct IT_keymap
{
  uint8_t  note;
  uint8_t  sample;
};

struct IT_node
{
  int8_t   value; /* Note: no padding in file. */
  uint16_t tick;
};

struct IT_envelope
{
  enum IT_envelope_flags
  {
    ENABLED = (1<<0),
    LOOP    = (1<<1),
    SUSTAIN = (1<<2),
    CARRY   = (1<<3),
    FILTER  = (1<<7), /* Sets pitch envelope to act as a filter envelope instead. */
  };

  /*  00 */ uint8_t  flags;
  /*  01 */ uint8_t  num_nodes;
  /*  02 */ uint8_t  loop_start;
  /*  03 */ uint8_t  loop_end;
  /*  04 */ uint8_t  sustain_start;
  /*  05 */ uint8_t  sustain_end;
  /*  06 */ IT_node  nodes[MAX_ENVELOPE];
};

struct IT_instrument
{
  /*  00 */ char     magic[4]; /* IMPI */
  /*  04 */ char     filename[13];
  /*  17 */ uint8_t  new_note_act;
  /*  18 */ uint8_t  duplicate_check_type;
  /*  19 */ uint8_t  duplicate_check_act;
  /*  20 */ uint16_t fadeout;
  /*  22 */ int8_t   pitch_pan_sep;
  /*  23 */ uint8_t  pitch_pan_center;
  /*  24 */ uint8_t  global_volume;
  /*  25 */ uint8_t  default_pan;
  /*  26 */ uint8_t  random_volume;
  /*  27 */ uint8_t  random_pan;
  /*  28 */ uint16_t tracker_version; /* Inst. files only. */
  /*  30 */ uint8_t  num_samples;     /* Inst. files only. */
  /*  31 */ uint8_t  pad;
  /*  32 */ char     name[26];
  /*  58 */ uint8_t  init_filter_cutoff;
  /*  59 */ uint8_t  init_filter_resonance;
  /*  60 */ uint8_t  midi_channel;
  /*  61 */ uint8_t  midi_program;
  /*  62 */ uint16_t midi_bank;
  /*  64 */ IT_keymap keymap[120];
  /* 304 */

  IT_envelope env_volume;
  IT_envelope env_pan;
  IT_envelope env_pitch;

  /* Derived values. */
  int real_default_pan;
  int real_init_filter_cutoff;
  int real_init_filter_resonance;
};

struct IT_sample
{
  /*  00 */ char     magic[4]; /* IMPS */
  /*  04 */ char     filename[13];
  /*  17 */ uint8_t  global_volume;
  /*  18 */ uint8_t  flags;
  /*  19 */ uint8_t  default_volume;
  /*  20 */ char     name[26];
  /*  46 */ uint8_t  convert;
  /*  47 */ uint8_t  default_pan;
  /*  48 */ uint32_t length; /* in samples, not bytes. */
  /*  52 */ uint32_t loop_start; /* in samples, not bytes. */
  /*  56 */ uint32_t loop_end; /* in samples, not bytes */
  /*  60 */ uint32_t c5_speed;
  /*  64 */ uint32_t sustain_loop_start;
  /*  68 */ uint32_t sustain_loop_end;
  /*  72 */ uint32_t sample_data_offset;
  /*  76 */ uint8_t  vibrato_speed;
  /*  77 */ uint8_t  vibrato_depth;
  /*  78 */ uint8_t  vibrato_waveform;
  /*  79 */ uint8_t  vibrato_rate;

  /* Derived values for compressed samples. */
  bool     scanned;
  uint32_t uncompressed_bytes;
  uint32_t compressed_bytes;
  uint32_t smallest_block;
  uint32_t smallest_block_samples;
  uint32_t largest_block;
};

struct IT_event
{
  enum
  {
    NOTE            = (1 << 0),
    INSTRUMENT      = (1 << 1),
    VOLUME          = (1 << 2),
    EFFECT          = (1 << 3),
    LAST_NOTE       = (1 << 4),
    LAST_INSTRUMENT = (1 << 5),
    LAST_VOLUME     = (1 << 6),
    LAST_EFFECT     = (1 << 7),

    CHANNEL         = 0x3f,
    READ_MASK       = (1 << 7),

    NO_VOLUME       = 0,
    SET_VOLUME      = 1,
    SET_PAN         = 2,
    FINE_VOLUME_UP  = 3,
    FINE_VOLUME_DN  = 4,
    VOLUME_UP       = 5,
    VOLUME_DN       = 6,
    PORTA_UP        = 7,
    PORTA_DN        = 8,
    TONEPORTA       = 9,
    VIBRATO         = 10,
    VOLUME_INVALID  = 11,
    NUM_VOLUME_FX   = 12
  };

  // If switching back to vector for some reason, 0 init these.
  uint8_t note;
  uint8_t instrument;
  uint8_t volume_effect;// = NO_VOLUME;
  uint8_t volume_param;
  uint8_t effect;
  uint8_t param;

  void set_volume(uint8_t volume)
  {
    if(volume <= 64)
    {
      volume_effect = SET_VOLUME;
      volume_param  = volume;
    }
    else

    if(volume >= 65 && volume <= 74)
    {
      volume_effect = FINE_VOLUME_UP;
      volume_param  = volume - 65;
    }
    else

    if(volume >= 75 && volume <= 84)
    {
      volume_effect = FINE_VOLUME_DN;
      volume_param  = volume - 75;
    }
    else

    if(volume >= 85 && volume <= 94)
    {
      volume_effect = VOLUME_UP;
      volume_param  = volume - 85;
    }
    else

    if(volume >= 95 && volume <= 104)
    {
      volume_effect = VOLUME_DN;
      volume_param  = volume - 95;
    }
    else

    if(volume >= 105 && volume <= 114)
    {
      volume_effect = PORTA_UP;
      volume_param  = volume - 105;
    }
    else

    if(volume >= 115 && volume <= 124)
    {
      volume_effect = PORTA_DN;
      volume_param  = volume - 115;
    }
    else

    if(volume >= 128 && volume <= 192)
    {
      volume_effect = SET_PAN;
      volume_param  = volume - 128;
    }
    else

    if(volume >= 193 && volume <= 202)
    {
      volume_effect = TONEPORTA;
      volume_param  = volume - 193;
    }
    else

    if(volume >= 203 && volume <= 212)
    {
      volume_effect = VIBRATO;
      volume_param  = volume - 203;
    }
    else
    {
      volume_effect = VOLUME_INVALID;
      volume_param  = volume;
    }
  }
};

struct IT_pattern
{
  // Using calloc instead of std::vector cuts ~20 seconds off of a
  // full scan of Modland's IT folder, a ~22% improvement when cached.
//  std::vector<IT_event> events;
  IT_event *events = nullptr;
  uint16_t raw_size_stored = 0;
  uint16_t raw_size = 0;
  uint16_t num_rows = 0;
  uint8_t  num_channels = 0;

  IT_pattern() {}
  ~IT_pattern()
  {
    free(events);
  }

  void allocate()
  {
//    events.resize(num_rows * num_channels);
    if(events)
      free(events);
    events = (IT_event *)calloc(num_rows * num_channels, sizeof(IT_event));
  }
};

struct IT_midiconfig
{
  char global[9][32];
  char sfx[16][32];
  char zxx[128][32];
};

struct IT_header
{
  /*  00 */ char     magic[4]; /* IMPM */
  /*  04 */ char     name[26];
  /*  30 */ uint16_t highlight;
  /*  32 */ uint16_t num_orders;
  /*  34 */ uint16_t num_instruments;
  /*  36 */ uint16_t num_samples;
  /*  38 */ uint16_t num_patterns;
  /*  40 */ uint16_t tracker_version;
  /*  42 */ uint16_t format_version;
  /*  44 */ uint16_t flags;
  /*  46 */ uint16_t special;

  /*  48 */ uint8_t  global_volume;
  /*  49 */ uint8_t  mix_volume;
  /*  50 */ uint8_t  initial_speed;
  /*  51 */ uint8_t  initial_tempo;
  /*  52 */ uint8_t  pan_separation;
  /*  53 */ uint8_t  midi_pitch_wheel;
  /*  54 */ uint16_t message_length;
  /*  56 */ uint32_t message_offset;
  /*  60 */ uint32_t reserved;

  /*  64 */ uint8_t  channel_pan[64];
  /* 128 */ uint8_t  channel_volume[64];
  /* 192 */
};

struct IT_data
{
  IT_header     header;
  IT_midiconfig midi;
  bool          uses[NUM_FEATURES];
  size_t        num_channels;

  std::vector<IT_sample>     samples;
  std::vector<IT_instrument> instruments;
  std::vector<IT_pattern>    patterns;
  std::vector<uint8_t>       orders;
  std::vector<uint32_t>      instrument_offsets;
  std::vector<uint32_t>      sample_offsets;
  std::vector<uint32_t>      pattern_offsets;

  std::vector<uint8_t>       workbuf;
};

/* Char 0 displays identically to a space (32) in name fields, but
 * not in filename fields.
 *
 * The string fields provide one extra uneditable char, presumably for
 * a terminator (even though char 0 doesn't terminate prior).
 */
template<int LEN>
static void IT_string_fix(char (&name)[LEN])
{
  for(int i = 0; i < LEN - 1; i++)
    if(name[i] == '\0')
      name[i] = ' ';

  name[LEN - 1] = '\0';
}

static bool IT_scan_compressed_sample(FILE *fp, IT_data &m, IT_sample &s)
{
  bool is_16_bit = !!(s.flags & SAMPLE_16_BIT);
  int block_num = 0;

  if(fseek(fp, s.sample_data_offset, SEEK_SET))
    return false;

  s.scanned = false;
  s.compressed_bytes = 0;
  s.uncompressed_bytes = s.length * (is_16_bit ? 2 : 1) * (s.flags & SAMPLE_STEREO ? 2 : 1);
  s.smallest_block = 0xffffffffu;
  s.smallest_block_samples = 0;
  s.largest_block = 0u;

  for(uint32_t pos = 0; pos < s.length; block_num++)
  {
    uint16_t block_uncompressed_samples = is_16_bit ? 0x4000 : 0x8000;
    uint16_t block_compressed_bytes = fget_u16le(fp);
    uint16_t bit_width = is_16_bit ? 17 : 9;

    if(feof(fp))
      return false;

    s.compressed_bytes += block_compressed_bytes + 2;

    if(s.length - pos < block_uncompressed_samples)
      block_uncompressed_samples = s.length - pos;

    if(block_compressed_bytes > s.largest_block)
      s.largest_block = block_compressed_bytes;

    if(block_compressed_bytes < s.smallest_block)
    {
      s.smallest_block = block_compressed_bytes;
      s.smallest_block_samples = block_uncompressed_samples;
    }

    if(!fread(m.workbuf.data(), block_compressed_bytes, 1, fp))
      return false;

    Bitstream bs(m.workbuf, block_compressed_bytes);
    //O_("block of size %u -> %u samples\n", block_compressed_bytes, block_uncompressed_samples);
    for(uint32_t i = 0; i < block_uncompressed_samples;)
    {
      ssize_t code = bs.read(bit_width);
      if(code < 0)
        break;

      if(bit_width >= 1 && bit_width <= 6)
      {
        if(code == (1 << (bit_width - 1)))
        {
          // Change bitwidth.
          ssize_t new_bit_width = bs.read(is_16_bit ? 4 : 3) + 1;
          if(new_bit_width <= 0)
            return false;

          bit_width = (new_bit_width < bit_width) ? new_bit_width : new_bit_width + 1;
          //O_("bit width now %u\n", bit_width);
          continue;
        }
        // Unpack sample.
        pos++;
        i++;
      }
      else

      if(bit_width <= (is_16_bit ? 16 : 8))
      {
        // trust in olivier lapicque's incomprehensible mess
        uint16_t a, b;
        if(is_16_bit)
        {
          a = (0xffff >> (17 - bit_width)) + 8;
          b = a - 16;
        }
        else
        {
          a = (0xff >> (9 - bit_width)) + 4;
          b = a - 8;
        }

        if(code > b && code <= a)
        {
          // Change bitwidth.
          code -= b;
          bit_width = (code < bit_width) ? code : code + 1;
          //O_("bit width now %u\n", bit_width);
          continue;
        }

        // Unpack sample.
        pos++;
        i++;
      }
      else

      if(bit_width == (is_16_bit ? 17 : 9))
      {
        if(code & (is_16_bit ? 0x10000 : 0x100))
        {
          // Change bitwidth.
          bit_width = (code & 0xff) + 1;
          //O_("bit_width now %u\n", bit_width);
          continue;
        }

        // Unpack sample.
        pos++;
        i++;
      }
      else
      {
        // Invalid width--prematurely end block.
        format::warning("invalid bit width %u in block %d", bit_width, block_num);
        m.uses[FT_SAMPLE_COMPRESSION_INVALID_WIDTH] = true;
        pos += block_uncompressed_samples - i;
        break;
      }
    }
  }
  s.scanned = true;
  return true;
}

/**
 * Read an IT sample.
 */
static modutil::error IT_read_sample(FILE *fp, IT_sample &s)
{
  if(!fread(s.magic, 4, 1, fp))
    return modutil::READ_ERROR;
  if(strncmp(s.magic, "IMPS", 4))
    return modutil::IT_INVALID_SAMPLE;

  if(!fread(s.filename, 13, 1, fp))
    return modutil::READ_ERROR;
  s.filename[12] = '\0';

  s.global_volume      = fgetc(fp);
  s.flags              = fgetc(fp);
  s.default_volume     = fgetc(fp);

  if(!fread(s.name, 26, 1, fp))
    return modutil::READ_ERROR;
  IT_string_fix(s.name);

  s.convert            = fgetc(fp);
  s.default_pan        = fgetc(fp);
  s.length             = fget_u32le(fp);
  s.loop_start         = fget_u32le(fp);
  s.loop_end           = fget_u32le(fp);
  s.c5_speed           = fget_u32le(fp);
  s.sustain_loop_start = fget_u32le(fp);
  s.sustain_loop_end   = fget_u32le(fp);
  s.sample_data_offset = fget_u32le(fp);
  s.vibrato_speed      = fgetc(fp);
  s.vibrato_depth      = fgetc(fp);
  s.vibrato_waveform   = fgetc(fp);
  s.vibrato_rate       = fgetc(fp);

  if(feof(fp))
    return modutil::READ_ERROR;

  return modutil::SUCCESS;
}

/**
 * Read an IT envelope.
 */
static modutil::error IT_read_envelope(FILE *fp, IT_envelope &env)
{
  env.flags         = fgetc(fp);
  env.num_nodes     = fgetc(fp);
  env.loop_start    = fgetc(fp);
  env.loop_end      = fgetc(fp);
  env.sustain_start = fgetc(fp);
  env.sustain_end   = fgetc(fp);

  static_assert(MAX_ENVELOPE >= 25, "wtf?");
  for(size_t i = 0; i < 25; i++)
  {
    env.nodes[i].value = fgetc(fp);
    env.nodes[i].tick  = fget_u16le(fp);
  }
  fgetc(fp); /* Padding byte. */
  if(feof(fp))
    return modutil::READ_ERROR;

  return modutil::SUCCESS;
}

/**
 * Read an IT instrument.
 */
static modutil::error IT_read_instrument(FILE *fp, IT_instrument &ins)
{
  if(!fread(ins.magic, 4, 1, fp))
    return modutil::READ_ERROR;
  if(strncmp(ins.magic, "IMPI", 4))
    return modutil::IT_INVALID_INSTRUMENT;

  if(!fread(ins.filename, 13, 1, fp))
    return modutil::READ_ERROR;
  ins.filename[12] = '\0';

  ins.new_note_act          = fgetc(fp);
  ins.duplicate_check_type  = fgetc(fp);
  ins.duplicate_check_act   = fgetc(fp);
  ins.fadeout               = fget_u16le(fp);
  ins.pitch_pan_sep         = fgetc(fp);
  ins.pitch_pan_center      = fgetc(fp);
  ins.global_volume         = fgetc(fp);
  ins.default_pan           = fgetc(fp);
  ins.random_volume         = fgetc(fp);
  ins.random_pan            = fgetc(fp);
  ins.tracker_version       = fget_u16le(fp);
  ins.num_samples           = fgetc(fp);
  ins.pad                   = fgetc(fp);

  if(!fread(ins.name, 26, 1, fp))
    return modutil::READ_ERROR;
  IT_string_fix(ins.name);

  ins.init_filter_cutoff    = fgetc(fp);
  ins.init_filter_resonance = fgetc(fp);
  ins.midi_channel          = fgetc(fp);
  ins.midi_program          = fgetc(fp);
  ins.midi_bank             = fget_u16le(fp);

  for(size_t i = 0; i < 120; i++)
  {
    ins.keymap[i].note   = fgetc(fp);
    ins.keymap[i].sample = fgetc(fp);
  }
  if(feof(fp))
    return modutil::READ_ERROR;

  modutil::error ret;
  ret = IT_read_envelope(fp, ins.env_volume);
  if(ret != modutil::SUCCESS)
    return ret;
  ret = IT_read_envelope(fp, ins.env_pan);
  if(ret != modutil::SUCCESS)
    return ret;
  ret = IT_read_envelope(fp, ins.env_pitch);
  if(ret != modutil::SUCCESS)
    return ret;

  /* Fix some variables. */
  ins.real_default_pan = (~ins.default_pan & 0x80) ? ins.default_pan : -1;
  ins.real_init_filter_cutoff = (ins.init_filter_cutoff & 0x80) ? ins.init_filter_cutoff & 0x7f : -1;
  ins.real_init_filter_resonance = (ins.init_filter_resonance & 0x80) ? ins.init_filter_resonance & 0x7f : -1;

  return modutil::SUCCESS;
}

/**
 * Read an IT instrument (1.x).
 */
static modutil::error IT_read_old_instrument(FILE *fp, IT_instrument &ins)
{
  if(!fread(ins.magic, 4, 1, fp))
    return modutil::READ_ERROR;
  if(strncmp(ins.magic, "IMPI", 4))
    return modutil::IT_INVALID_INSTRUMENT;

  if(!fread(ins.filename, 13, 1, fp))
    return modutil::READ_ERROR;
  ins.filename[12] = '\0';

  IT_envelope &env = ins.env_volume;

  env.flags         = fgetc(fp);
  env.loop_start    = fgetc(fp);
  env.loop_end      = fgetc(fp);
  env.sustain_start = fgetc(fp);
  env.sustain_end   = fgetc(fp);
  fgetc(fp);
  fgetc(fp);

  ins.fadeout              = fget_u16le(fp) << 1;
  ins.new_note_act         = fgetc(fp);
  ins.duplicate_check_type = fgetc(fp) & 1;
  ins.duplicate_check_act  = 1;
  ins.tracker_version      = fget_u16le(fp);
  ins.num_samples          = fgetc(fp);
  ins.pad                  = fgetc(fp);

  if(!fread(ins.name, 26, 1, fp))
    return modutil::READ_ERROR;
  IT_string_fix(ins.name);
  fgetc(fp);
  fgetc(fp);
  fgetc(fp);
  fgetc(fp);
  fgetc(fp);
  fgetc(fp);

  for(size_t i = 0; i < 120; i++)
  {
    ins.keymap[i].note   = fgetc(fp);
    ins.keymap[i].sample = fgetc(fp);
  }
  if(feof(fp))
    return modutil::READ_ERROR;

  /* Envelope points (??) */
  if(fseek(fp, 200, SEEK_CUR))
    return modutil::SEEK_ERROR;

  static_assert(MAX_ENVELOPE >= 25, "wtf?");
  size_t num_nodes;
  for(num_nodes = 0; num_nodes < 25; num_nodes++)
  {
    env.nodes[num_nodes].tick  = fgetc(fp);
    env.nodes[num_nodes].value = fgetc(fp);
  }
  env.num_nodes = num_nodes;
  if(feof(fp))
    return modutil::READ_ERROR;

  /* These don't exist for old instruments. */
  ins.real_default_pan = -1;
  ins.real_init_filter_cutoff = -1;
  ins.real_init_filter_resonance = -1;

  return modutil::SUCCESS;
}

/**
 * Scan an IT pattern. This is required to determine the number of stored channels.
 */
static modutil::error IT_scan_pattern(IT_pattern &p, const std::vector<uint8_t> &stream)
{
  p.num_channels = 0;

  uint8_t mask[64]{};
  for(size_t row = 0, i = 0; row < p.num_rows && i < p.raw_size;)
  {
    size_t flags = stream[i++];
    if(flags == 0)
    {
      row++;
      continue;
    }

    size_t channel = (flags - 1) & IT_event::CHANNEL;
    if(channel >= p.num_channels)
      p.num_channels = channel + 1;

    if(flags & IT_event::READ_MASK)
    {
      if(p.raw_size - i < 1)
        return modutil::INVALID;

      mask[channel] = stream[i++];
    }

    size_t skip = 0;

    if(mask[channel] & IT_event::NOTE)
      skip += 1;
    if(mask[channel] & IT_event::INSTRUMENT)
      skip += 1;
    if(mask[channel] & IT_event::VOLUME)
      skip += 1;
    if(mask[channel] & IT_event::EFFECT)
      skip += 2;

    if(p.raw_size - i < skip)
      return modutil::INVALID;

    i += skip;
  }
  return modutil::SUCCESS;
}

/**
 * Read an IT pattern.
 */
struct last_event
{
  uint8_t note;
  uint8_t instrument;
  uint8_t volume;
  uint8_t effect;
  uint8_t param;
};

static modutil::error IT_read_pattern(IT_data &m, IT_pattern &p, const std::vector<uint8_t> &stream)
{
  if(p.num_rows < 1 || p.num_channels < 1)
    return modutil::SUCCESS;

  p.allocate();

  uint8_t mask[64]{};
  last_event last_events[64]{};

  for(size_t row = 0, i = 0; row < p.num_rows && i < p.raw_size;)
  {
    size_t flags = stream[i++];
    if(flags == 0)
    {
      row++;
      continue;
    }

    size_t channel = (flags - 1) & IT_event::CHANNEL;
    if(channel >= p.num_channels)
      return modutil::INVALID;

    last_event &last = last_events[channel];

    if(flags & IT_event::READ_MASK)
    {
      if(p.raw_size - i < 1)
        return modutil::INVALID;

      mask[channel] = stream[i++];
    }

    IT_event &ev = p.events[row * p.num_channels + channel];
    if(mask[channel] & IT_event::NOTE)
    {
      if(p.raw_size - i < 1)
        return modutil::INVALID;

      ev.note = last.note = stream[i++];
    }

    if(mask[channel] & IT_event::INSTRUMENT)
    {
      if(p.raw_size - i < 1)
        return modutil::INVALID;

      ev.instrument = last.instrument = stream[i++];
    }

    if(mask[channel] & IT_event::VOLUME)
    {
      if(p.raw_size - i < 1)
        return modutil::INVALID;

      last.volume = stream[i++];
      ev.set_volume(last.volume);
    }

    if(mask[channel] & IT_event::EFFECT)
    {
      if(p.raw_size - i < 2)
        return modutil::INVALID;

      ev.effect = last.effect = stream[i++];
      ev.param  = last.param  = stream[i++];
    }

    if(mask[channel] & IT_event::LAST_NOTE)
      ev.note = last.note;
    if(mask[channel] & IT_event::LAST_INSTRUMENT)
      ev.instrument = last.instrument;
    if(mask[channel] & IT_event::LAST_VOLUME)
      ev.set_volume(last.volume);
    if(mask[channel] & IT_event::LAST_EFFECT)
    {
      ev.effect = last.effect;
      ev.param  = last.param;
    }

    if(ev.effect == ('S'-'@') && (ev.param >> 4) == 0xf)
      m.uses[FT_E_MACROSET] = true;
    if(ev.effect == ('Z'-'@'))
      m.uses[FT_E_MACRO] = true;
    if(ev.effect == ('\\'-'@'))
      m.uses[FT_E_MACROSMOOTH] = true;
  }
  return modutil::SUCCESS;
}


/**
 * Read an IT file.
 */
static modutil::error IT_read(FILE *fp)
{
  IT_data m{};
  IT_header &h = m.header;

  if(!fread(h.magic, 4, 1, fp))
    return modutil::FORMAT_ERROR;

  if(strncmp(h.magic, "IMPM", 4))
    return modutil::FORMAT_ERROR;

  num_its++;

  if(!fread(h.name, 26, 1, fp))
    return modutil::READ_ERROR;
  IT_string_fix(h.name);

  h.highlight        = fget_u16le(fp);
  h.num_orders       = fget_u16le(fp);
  h.num_instruments  = fget_u16le(fp);
  h.num_samples      = fget_u16le(fp);
  h.num_patterns     = fget_u16le(fp);
  h.tracker_version  = fget_u16le(fp);
  h.format_version   = fget_u16le(fp);
  h.flags            = fget_u16le(fp);
  h.special          = fget_u16le(fp);
  h.global_volume    = fgetc(fp);
  h.mix_volume       = fgetc(fp);
  h.initial_speed    = fgetc(fp);
  h.initial_tempo    = fgetc(fp);
  h.pan_separation   = fgetc(fp);
  h.midi_pitch_wheel = fgetc(fp);
  h.message_length   = fget_u16le(fp);
  h.message_offset   = fget_u32le(fp);
  h.reserved         = fget_u32le(fp);

  if(!fread(h.channel_pan, 64, 1, fp))
    return modutil::READ_ERROR;
  if(!fread(h.channel_volume, 64, 1, fp))
    return modutil::READ_ERROR;

  if(h.format_version < 0x200)
    m.uses[FT_OLD_FORMAT] = true;

  if(h.flags & F_INST_MODE)
    m.uses[FT_INSTRUMENT_MODE] = true;
  else
    m.uses[FT_SAMPLE_MODE] = true;

  if(h.flags & F_MIDI_CONFIG)
    m.uses[FT_MIDI_CONFIG] = true;

  if(h.num_orders)
  {
    m.orders.resize(h.num_orders);
    if(!fread(m.orders.data(), h.num_orders, 1, fp))
      return modutil::READ_ERROR;
  }

  if(h.num_instruments && (h.flags & F_INST_MODE))
  {
    m.instrument_offsets.resize(h.num_instruments);
    for(size_t i = 0; i < h.num_instruments; i++)
      m.instrument_offsets[i] = fget_u32le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;
  }

  if(h.num_samples)
  {
    m.sample_offsets.resize(h.num_samples);
    for(size_t i = 0; i < h.num_samples; i++)
      m.sample_offsets[i] = fget_u32le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;
  }

  if(h.num_patterns)
  {
    m.pattern_offsets.resize(h.num_patterns);
    for(size_t i = 0; i < h.num_patterns; i++)
      m.pattern_offsets[i] = fget_u32le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;
  }

  /* "Read extra info"? */
  {
    uint16_t skip = fget_u16le(fp);
    if(feof(fp) || (skip && fseek(fp, skip * 8, SEEK_CUR) < 0))
      return modutil::READ_ERROR;
  }

  /* Macro parameters */
  if(h.flags & F_MIDI_CONFIG)
  {
    IT_midiconfig &midi = m.midi;
    for(int i = 0; i < arraysize(midi.global); i++)
    {
      if(fread(midi.global[i], 1, 32, fp) < 32)
        return modutil::READ_ERROR;
      midi.global[i][31] = '\0';
    }
    for(int i = 0; i < arraysize(midi.sfx); i++)
    {
      if(fread(midi.sfx[i], 1, 32, fp) < 32)
        return modutil::READ_ERROR;
      midi.sfx[i][31] = '\0';
    }
    for(int i = 0; i < arraysize(midi.zxx); i++)
    {
      if(fread(midi.zxx[i], 1, 32, fp) < 32)
        return modutil::READ_ERROR;
      midi.zxx[i][31] = '\0';
    }
  }

  /* MPT extension: pattern names */
  /* MPT extension: channel names */

  /* Buffer used for pattern data and checks on sample compression. */
  m.workbuf.resize(65536);

  /* Load instruments. */
  if(h.num_instruments && (h.flags & F_INST_MODE))
  {
    m.instruments.resize(h.num_instruments, {});
    for(size_t i = 0; i < h.num_instruments; i++)
    {
      if(m.instrument_offsets[i] == 0)
        continue;

      if(fseek(fp, m.instrument_offsets[i], SEEK_SET))
        return modutil::SEEK_ERROR;

      IT_instrument &ins = m.instruments[i];

      modutil::error ret;
      if(h.format_version >= 0x200)
        ret = IT_read_instrument(fp, ins);
      else
        ret = IT_read_old_instrument(fp, ins);

      if(ret != modutil::SUCCESS)
      {
        format::warning("failed to load instrument %zu: %s", i, modutil::strerror(ret));
        continue;
      }

      if(ins.env_volume.flags & IT_envelope::ENABLED)
        m.uses[FT_ENV_VOLUME] = true;

      if(ins.env_pan.flags & IT_envelope::ENABLED)
        m.uses[FT_ENV_PAN] = true;

      if(ins.env_pitch.flags & IT_envelope::ENABLED)
      {
        if(ins.env_pitch.flags & IT_envelope::FILTER)
          m.uses[FT_ENV_FILTER] = true;
        else
          m.uses[FT_ENV_PITCH] = true;
      }
    }
  }

  /* Load samples. */
  if(h.num_samples)
  {
    m.samples.resize(h.num_samples, {});
    for(size_t i = 0; i < h.num_samples; i++)
    {
      if(m.sample_offsets[i] == 0)
        continue;

      if(fseek(fp, m.sample_offsets[i], SEEK_SET))
        return modutil::SEEK_ERROR;

      IT_sample &s = m.samples[i];

      modutil::error ret = IT_read_sample(fp, s);
      if(ret != modutil::SUCCESS)
      {
        format::warning("failed to load sample %zu: %s", i, modutil::strerror(ret));
        continue;
      }

      if(s.global_volume < 0x40)
        m.uses[FT_SAMPLE_GLOBAL_VOLUME] = true;

      if(s.vibrato_depth)
        m.uses[FT_SAMPLE_VIBRATO] = true;

      if(s.flags & SAMPLE_COMPRESSED)
        m.uses[FT_SAMPLE_COMPRESSION] = true;

      if(s.flags & SAMPLE_STEREO)
        m.uses[FT_SAMPLE_STEREO] = true;

      if(s.flags & SAMPLE_16_BIT)
        m.uses[FT_SAMPLE_16] = true;

      if(s.convert == 0xff)
        m.uses[FT_SAMPLE_ADPCM] = true;
    }
  }

  /* Scan sample compression data. */
  if(h.num_samples && m.uses[FT_SAMPLE_COMPRESSION])
  {
    for(unsigned int i = 0; i < h.num_samples; i++)
    {
      IT_sample &s = m.samples[i];
      if(!(s.flags & SAMPLE_COMPRESSED))
        continue;

      bool res = IT_scan_compressed_sample(fp, m, s);
      if(res)
      {
        /* Theoretical minimum size is 1 bit per sample.
         * Potentially samples can go lower if certain alleged quirks re: large bit widths are true.
         */
        if(s.compressed_bytes * 8 < s.length)
        {
          m.uses[FT_SAMPLE_COMPRESSION_1_8TH] = true;
        }
        else

        if(s.compressed_bytes * 4 < s.length)
          m.uses[FT_SAMPLE_COMPRESSION_1_4TH] = true;
      }
      else
        format::warning("failed to scan compressed sample %u", i);
    }
  }

  /* Load patterns. */
  if(h.num_patterns)
  {
    m.patterns.resize(h.num_patterns, {});

    for(size_t i = 0; i < h.num_patterns; i++)
    {
      if(m.pattern_offsets[i] == 0)
        continue;

      if(fseek(fp, m.pattern_offsets[i], SEEK_SET))
        return modutil::SEEK_ERROR;

      IT_pattern &p = m.patterns[i];

      /* Header. */
      p.raw_size = fget_u16le(fp);
      p.num_rows = fget_u16le(fp);
      fget_u32le(fp);

      p.raw_size_stored = p.raw_size;

      if(!p.raw_size || !p.num_rows)
        continue;

      /* Load even if the read is short or if the scan fails
       * since some software (libxmp) will also do this. */
      p.raw_size = fread(m.workbuf.data(), 1, p.raw_size, fp);

      if(p.raw_size < p.raw_size_stored)
        format::warning("read error at pattern %zu", i);

      modutil::error ret = IT_scan_pattern(p, m.workbuf);
      modutil::error ret2 = IT_read_pattern(m, p, m.workbuf);
      if(ret || ret2)
        format::warning("error loading pattern %zu", i);
    }
  }

  format::line("Name",     "%s", h.name);
  format::line("Type",     "IT %x (T:%x %03x)", h.format_version, (h.tracker_version >> 12), (h.tracker_version & 0xFFF));
  format::line("Samples",  "%u", h.num_samples);
  if(h.flags & F_INST_MODE)
    format::line("Instr.",   "%u", h.num_instruments);
  format::line("Patterns", "%u", h.num_patterns);
  format::line("Orders",   "%u", h.num_orders);
  format::line("Mix Vol.", "%u", h.mix_volume);
  format::uses(m.uses, FEATURE_STR);

  namespace table = format::table;

  if(Config.dump_samples)
  {
    /* Instruments */
    if(h.flags & F_INST_MODE)
    {
      static const char *labels[] =
      {
        "Name", "Filename", "NNA", "DCT", "DCA", "Fade",
        "GV", "RV", "Env", "DP", "RP", "PPS", "PPC", "Env", "IFC", "IFR", "Env"
      };
      format::line();
      table::table<
        table::string<25>,
        table::string<12>,
        table::spacer,
        table::string<4>,
        table::string<4>,
        table::string<4>,
        table::number<5>,
        table::spacer,
        table::number<3>,
        table::number<3>,
        table::string<4>,
        table::spacer,
        table::number<3>,
        table::number<3>,
        table::number<4>,
        table::number<3>,
        table::string<4>,
        table::spacer,
        table::number<3>,
        table::number<3>,
        table::string<4>> i_table;

      i_table.header("Instr.", labels);

      for(unsigned int i = 0; i < h.num_instruments; i++)
      {
        IT_instrument &ins = m.instruments[i];
        char flagvol[5];
        char flagpan[5];
        char flagpitch[5];

#define ENV_FLAGS(flags, str, is_pitch) do{ \
  str[0] = (flags & IT_envelope::ENABLED) ? 'e' : '\0'; \
  str[1] = (flags & IT_envelope::LOOP) ? 'L' : ' '; \
  str[2] = (flags & IT_envelope::SUSTAIN) ? 'S' : ' '; \
  str[3] = (flags & IT_envelope::CARRY) ? 'C' : ' '; \
  str[4] = '\0'; \
  if(is_pitch && str[0]) \
    str[0] = (flags & IT_envelope::FILTER) ? 'f' : 'p'; \
}while(0)

        ENV_FLAGS(ins.env_volume.flags, flagvol, false);
        ENV_FLAGS(ins.env_pan.flags, flagpan, false);
        ENV_FLAGS(ins.env_pitch.flags, flagpitch, true);

        i_table.row(i + 1, ins.name, ins.filename, {},
          NNA_string(ins.new_note_act),
          DCT_string(ins.duplicate_check_type),
          DCA_string(ins.duplicate_check_act),
          ins.fadeout, {},
          ins.global_volume, ins.random_volume, flagvol, {},
          ins.real_default_pan, ins.random_pan, ins.pitch_pan_sep, ins.pitch_pan_center, flagpan, {},
          ins.real_init_filter_cutoff, ins.real_init_filter_resonance, flagpitch
        );
      }
    }

    /* Samples */
    static const char *s_labels[] =
    {
      "Name", "Filename", "Length", "LoopStart", "LoopEnd", "Sus.Start", "Sus.End",
      "C5 Speed", "GV", "DV", "DP", "Cvt", "Flags", "VSp", "VDp", "VWf", "VRt",
    };
    format::line();
    table::table<
      table::string<25>,
      table::string<12>,
      table::spacer,
      table::number<10>,
      table::number<10>,
      table::number<10>,
      table::number<10>,
      table::number<10>,
      table::spacer,
      table::number<10>,
      table::number<3>,
      table::number<3>,
      table::number<3>,
      table::number<3>,
      table::string<8>,
      table::spacer,
      table::number<3>,
      table::number<3>,
      table::number<3>,
      table::number<3>> s_table;

    s_table.header("Samples", s_labels);

    for(unsigned int i = 0; i < h.num_samples; i++)
    {
      IT_sample &s = m.samples[i];
      char flagstr[9];

      flagstr[0] = !(s.flags & SAMPLE_SET) ? '-' : ' ';
      flagstr[1] = (s.flags & SAMPLE_16_BIT) ? 'W' : '.';
      flagstr[2] = (s.flags & SAMPLE_STEREO) ? 'S' : '.';
      flagstr[3] = (s.flags & SAMPLE_COMPRESSED) ? 'X' : ' ';
      flagstr[4] = (s.flags & SAMPLE_LOOP) ? 'L' : ' ';
      flagstr[5] = (s.flags & SAMPLE_BIDI_LOOP) ? 'b' : ' ';
      flagstr[6] = (s.flags & SAMPLE_SUSTAIN_LOOP) ? 'S' : ' ';
      flagstr[7] = (s.flags & SAMPLE_BIDI_SUSTAIN_LOOP) ? 'b' : ' ';
      flagstr[8] = '\0';

      s_table.row(i + 1, s.name, s.filename, {},
        s.length, s.loop_start, s.loop_end, s.sustain_loop_start, s.sustain_loop_end, {},
        s.c5_speed, s.global_volume, s.default_volume, s.default_pan, s.convert, flagstr, {},
        s.vibrato_speed, s.vibrato_depth, s.vibrato_waveform, s.vibrato_rate
      );
    }

    if(m.uses[FT_SAMPLE_COMPRESSION])
    {
      static const char *cmp_labels[] =
      {
        "Scan?", "CmpBytes", "UncmpBytes", "Min.Block", "Min.Smpls.", "Max.Block"
      };
      format::line();
      table::table<
        table::string<6>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>> cmp_table;

      cmp_table.header("Smp.Cmp.", cmp_labels);

      for(unsigned int i = 0; i < h.num_samples; i++)
      {
        IT_sample &s = m.samples[i];
        if(!(s.flags & SAMPLE_COMPRESSED))
          continue;

        cmp_table.row(i + 1, s.scanned ? "pass" : "fail",
          s.compressed_bytes, s.uncompressed_bytes, {},
          s.smallest_block, s.smallest_block_samples, s.largest_block
        );
      }
    }
  }

  if(Config.dump_patterns)
  {
    format::line();
    format::orders("Orders", m.orders.data(), h.num_orders);

    /* Print MIDI macro configuration */
    if(h.flags & F_MIDI_CONFIG)
    {
      IT_midiconfig &midi = m.midi;

      static const char *midi_labels[] = { "MIDI Message" };
      table::table<
        table::string<32>> midi_table;

      format::line();
      midi_table.header("Global", midi_labels);
      for(int i = 0; i < arraysize(midi.global); i++)
        if(midi.global[i][0])
          midi_table.row(i, midi.global[i]);

      format::line();
      midi_table.header("SFx", midi_labels);
      for(int i = 0; i < arraysize(midi.sfx); i++)
        if(midi.sfx[i][0])
          midi_table.row(i, midi.sfx[i]);

      format::line();
      midi_table.header("Zxx", midi_labels);
      for(int i = 0; i < arraysize(midi.zxx); i++)
        if(midi.zxx[i][0])
          midi_table.row(i, midi.zxx[i]);
    }

    if(!Config.dump_pattern_rows)
      format::line();

    for(size_t i = 0; i < h.num_patterns; i++)
    {
      IT_pattern &p = m.patterns[i];

      /* lol */
      static constexpr char chrs[IT_event::NUM_VOLUME_FX] =
      {
        ' ', 'v', 'p', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', '?'
      };

      struct volumeIT
      {
        uint8_t volume_effect;
        uint8_t volume_param;
        static constexpr int width() { return 4; }
        bool can_print() const { return volume_effect != IT_event::NO_VOLUME; }
        void print() const { if(can_print()) fprintf(stderr, " %c%02x", chrs[volume_effect], volume_param); else format::spaces(width()); }
      };

      using EVENT = format::event<format::note, format::sample, volumeIT, format::effectIT>;
      format::pattern<EVENT> pattern(i, p.num_channels, p.num_rows, p.raw_size);

      if(p.raw_size != p.raw_size_stored)
        pattern.extra("Expected packed size: %u", p.raw_size_stored);

      if(!Config.dump_pattern_rows)
      {
        pattern.summary();
        continue;
      }
      if(!p.events)
      {
        pattern.print();
        continue;
      }

      IT_event *current = p.events;
      for(size_t row = 0; row < p.num_rows; row++)
      {
        for(size_t track = 0; track < p.num_channels; track++, current++)
        {
          format::note     a{ current->note };
          format::sample   b{ current->instrument };
          volumeIT         c{ current->volume_effect, current->volume_param };
          format::effectIT d{ current->effect, current->param };

          pattern.insert(EVENT(a, b, c, d));
        }
      }
      pattern.print();
    }
  }

  return modutil::SUCCESS;
}


class IT_loader : public modutil::loader
{
public:
  IT_loader(): modutil::loader("IT", "it", "Impulse Tracker") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    return IT_read(fp);
  }

  virtual void report() const override
  {
    if(!num_its)
      return;

    format::report("Total ITs", num_its);
  }
};

static const IT_loader loader;
