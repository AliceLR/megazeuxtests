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

static int num_its;
//static int num_it_instrument_mode;
//static int num_it_sample_gvol;
//static int num_it_sample_vibrato;


enum IT_features
{
  FT_OLD_FORMAT,
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
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "<2.00",
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
};

struct IT_data
{
  IT_header     header;
  IT_sample     *samples = nullptr;
  IT_instrument *instruments = nullptr;
  uint8_t       *orders = nullptr;
  uint32_t      *instrument_offsets = nullptr;
  uint32_t      *sample_offsets = nullptr;
  uint32_t      *pattern_offsets = nullptr;
  bool          uses[NUM_FEATURES];

  ~IT_data()
  {
    delete[] samples;
    delete[] instruments;
    delete[] orders;
    delete[] instrument_offsets;
    delete[] sample_offsets;
    delete[] pattern_offsets;
  }
};

class Bitstream
{
private:
  FILE *in;
  size_t in_length;
  size_t in_total_bytes = 0;
  uint32_t in_buffer = 0;
  uint32_t in_bits = 0;

public:
  Bitstream(FILE *fp, size_t max_length): in(fp), in_length(max_length) {}

  bool end_of_block()
  {
    return in_total_bytes >= in_length;
  }

  ssize_t read_bits(uint32_t bits)
  {
    if(bits > 24)
      return -1;

    if(in_bits < bits)
    {
      if(end_of_block())
        return -1;

      while(in_bits < 24)
      {
        int b = fgetc(in);
        if(b < 0)
        {
          if(ferror(in))
            return -1;
          break;
        }

        in_buffer |= (b << in_bits);
        in_bits += 8;
        in_total_bytes++;

        if(end_of_block())
          break;
      }

      if(in_bits < bits)
        return -1;
    }

    ssize_t ret = in_buffer & ((1 << bits) - 1);
    in_buffer >>= bits;
    in_bits -= bits;
    return ret;
  }
};

static bool IT_scan_compressed_sample(FILE *fp, IT_data &m, IT_sample &s)
{
  bool is_16_bit = !!(s.flags & SAMPLE_16_BIT);

  if(fseek(fp, s.sample_data_offset, SEEK_SET))
    return false;

  s.scanned = false;
  s.compressed_bytes = 0;
  s.uncompressed_bytes = s.length * (is_16_bit ? 2 : 1) * (s.flags & SAMPLE_STEREO ? 2 : 1);
  s.smallest_block = 0xffffffffu;
  s.smallest_block_samples = 0;
  s.largest_block = 0u;

  for(uint32_t pos = 0; pos < s.length;)
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

    Bitstream bs(fp, block_compressed_bytes);
    //O_("block of size %u -> %u samples\n", block_compressed_bytes, block_uncompressed_samples);
    for(uint32_t i = 0; i < block_uncompressed_samples;)
    {
      ssize_t code = bs.read_bits(bit_width);
      if(code < 0)
      {
        if(bs.end_of_block())
          break;
        else
          return false;
      }

      if(bit_width >= 1 && bit_width <= 6)
      {
        if(code == (1 << (bit_width - 1)))
        {
          // Change bitwidth.
          ssize_t new_bit_width = bs.read_bits(is_16_bit ? 4 : 3) + 1;
          if(new_bit_width < 0)
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
        format::warning("invalid bit width %u", bit_width);
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
  s.name[25] = '\0';

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
  ins.name[25] = '\0';

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
  ins.name[25] = '\0';
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
 * Read an IT file.
 */
static modutil::error IT_read(FILE *fp)
{
  IT_data m{};
  IT_header &h = m.header;

  if(!fread(h.magic, 4, 1, fp))
    return modutil::READ_ERROR;

  if(strncmp(h.magic, "IMPM", 4))
    return modutil::FORMAT_ERROR;

  if(!fread(h.name, 26, 1, fp))
    return modutil::READ_ERROR;
  h.name[25] = '\0';

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

  if(h.num_orders)
  {
    m.orders = new uint8_t[h.num_orders];
    if(!fread(m.orders, h.num_orders, 1, fp))
      return modutil::READ_ERROR;
  }

  if(h.num_instruments && (h.flags & F_INST_MODE))
  {
    m.instrument_offsets = new uint32_t[h.num_instruments];
    for(size_t i = 0; i < h.num_instruments; i++)
      m.instrument_offsets[i] = fget_u32le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;
  }

  if(h.num_samples)
  {
    m.sample_offsets = new uint32_t[h.num_samples];
    for(size_t i = 0; i < h.num_samples; i++)
      m.sample_offsets[i] = fget_u32le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;
  }

  if(h.num_patterns)
  {
    m.pattern_offsets = new uint32_t[h.num_patterns];
    for(size_t i = 0; i < h.num_patterns; i++)
      m.pattern_offsets[i] = fget_u32le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;
  }

  /* Load instruments. */
  if(h.num_instruments && (h.flags & F_INST_MODE))
  {
    m.instruments = new IT_instrument[h.num_instruments]{};
    for(size_t i = 0; i < h.num_instruments; i++)
    {
      if(fseek(fp, m.instrument_offsets[i], SEEK_SET))
        return modutil::SEEK_ERROR;

      IT_instrument &ins = m.instruments[i];

      if(h.format_version >= 0x200)
      {
        modutil::error ret = IT_read_instrument(fp, ins);
        if(ret != modutil::SUCCESS)
          return ret;
      }
      else
      {
        modutil::error ret = IT_read_old_instrument(fp, ins);
        if(ret != modutil::SUCCESS)
          return ret;
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
    m.samples = new IT_sample[h.num_samples];
    for(size_t i = 0; i < h.num_samples; i++)
    {
      if(fseek(fp, m.sample_offsets[i], SEEK_SET))
        return modutil::SEEK_ERROR;

      IT_sample &s = m.samples[i];

      modutil::error ret = IT_read_sample(fp, s);
      if(ret != modutil::SUCCESS)
        return ret;

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
  if(m.samples && m.uses[FT_SAMPLE_COMPRESSION])
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
        if(s.compressed_bytes < s.length / 8)
        {
          m.uses[FT_SAMPLE_COMPRESSION_1_8TH] = true;
        }
        else

        if(s.compressed_bytes < s.length / 4)
          m.uses[FT_SAMPLE_COMPRESSION_1_4TH] = true;
      }
      else
        format::warning("failed to scan compressed sample %u", i);
    }
  }

  /* TODO load patterns. */

  format::line("Name",     "%s", h.name);
  format::line("Type",     "IT %x (T:%x %03x)", h.format_version, (h.tracker_version >> 12), (h.tracker_version & 0xFFF));
  format::line("Samples",  "%u", h.num_samples);
  if(h.flags & F_INST_MODE)
    format::line("Instr.",   "%u", h.num_instruments);
  format::line("Patterns", "%u", h.num_patterns);
  format::line("Orders",   "%u", h.num_orders);
  format::uses(m.uses, FEATURE_STR);

  if(Config.dump_samples)
  {
    namespace table = format::table;

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
    // FIXME
  }

  num_its++;
  return modutil::SUCCESS;
}


class IT_loader : public modutil::loader
{
public:
  IT_loader(): modutil::loader("IT", "Impulse Tracker") {}

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
