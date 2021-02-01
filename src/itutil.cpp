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

#include "Config.hpp"
#include "common.hpp"

static const char USAGE[] =
  "Dump various information from IT module(s).\n\n"
  "Usage:\n"
  "  itutil [options] [it files...]\n\n";

static int num_its;
//static int num_it_instrument_mode;
//static int num_it_sample_gvol;
//static int num_it_sample_vibrato;

enum IT_error
{
  IT_SUCCESS,
  IT_READ_ERROR,
  IT_SEEK_ERROR,
  IT_INVALID_MAGIC,
  IT_INVALID_SAMPLE,
  IT_INVALID_ORDER_COUNT,
  IT_INVALID_PATTERN_COUNT,
};

static const char *IT_strerror(int err)
{
  switch(err)
  {
    case IT_SUCCESS: return "no error";
    case IT_READ_ERROR: return "read error";
    case IT_SEEK_ERROR: return "seek error";
    case IT_INVALID_MAGIC: return "file is not an IT";
    case IT_INVALID_SAMPLE: return "IT sample magic mismatch";
    case IT_INVALID_ORDER_COUNT: return "invalid order count >256";
    case IT_INVALID_PATTERN_COUNT: return "invalid pattern count >256";
  }
  return "unknown error";
}
enum IT_features
{
  FT_OLD_FORMAT,
  FT_INSTRUMENT_MODE,
  FT_SAMPLE_GLOBAL_VOLUME,
  FT_SAMPLE_VIBRATO,
  FT_SAMPLE_COMPRESSION,
  FT_SAMPLE_COMPRESSION_1_4TH,
  FT_SAMPLE_COMPRESSION_1_8TH,
  FT_SAMPLE_COMPRESSION_INVALID_WIDTH,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "<2.00",
  "InstMode",
  "SmpGVL",
  "SmpVib",
  "SmpCmp",
  "SmpCmp<1/4th",
  "SmpCmp<1/8th",
  "SmpCmpInvalidBW",
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
  IT_header header;
  IT_sample *samples = nullptr;
  uint8_t   *orders = nullptr;
  uint32_t  *instrument_offsets = nullptr;
  uint32_t  *sample_offsets = nullptr;
  uint32_t  *pattern_offsets = nullptr;
  bool      uses[NUM_FEATURES];

  ~IT_data()
  {
    delete[] samples;
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
        O_("Warning : invalid bit width %u wtf\n", bit_width);
        m.uses[FT_SAMPLE_COMPRESSION_INVALID_WIDTH] = true;
        pos += block_uncompressed_samples - i;
        break;
      }
    }
  }
  s.scanned = true;
  return true;
}

static int IT_read(FILE *fp)
{
  IT_data m{};
  IT_header &h = m.header;

  if(!fread(h.magic, 4, 1, fp))
    return IT_READ_ERROR;

  if(strncmp(h.magic, "IMPM", 4))
    return IT_INVALID_MAGIC;

  if(!fread(h.name, 26, 1, fp))
    return IT_READ_ERROR;
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
    return IT_READ_ERROR;
  if(!fread(h.channel_volume, 64, 1, fp))
    return IT_READ_ERROR;

  if(h.format_version < 0x200)
    m.uses[FT_OLD_FORMAT] = true;

  if(h.flags & F_INST_MODE)
    m.uses[FT_INSTRUMENT_MODE] = true;

  if(h.num_orders)
  {
    m.orders = new uint8_t[h.num_orders];
    if(!fread(m.orders, h.num_orders, 1, fp))
      return IT_READ_ERROR;
  }

  if(h.num_instruments && (h.flags & F_INST_MODE))
  {
    m.instrument_offsets = new uint32_t[h.num_instruments];
    for(size_t i = 0; i < h.num_instruments; i++)
      m.instrument_offsets[i] = fget_u32le(fp);
    if(feof(fp))
      return IT_READ_ERROR;
  }

  if(h.num_samples)
  {
    m.sample_offsets = new uint32_t[h.num_samples];
    for(size_t i = 0; i < h.num_samples; i++)
      m.sample_offsets[i] = fget_u32le(fp);
    if(feof(fp))
      return IT_READ_ERROR;
  }

  if(h.num_patterns)
  {
    m.pattern_offsets = new uint32_t[h.num_patterns];
    for(size_t i = 0; i < h.num_patterns; i++)
      m.pattern_offsets[i] = fget_u32le(fp);
    if(feof(fp))
      return IT_READ_ERROR;
  }

  /* TODO load instruments. */

  /* Load samples. */
  if(h.num_samples)
  {
    m.samples = new IT_sample[h.num_samples];
    for(size_t i = 0; i < h.num_samples; i++)
    {
      if(fseek(fp, m.sample_offsets[i], SEEK_SET))
        return IT_SEEK_ERROR;

      IT_sample &s = m.samples[i];

      if(!fread(s.magic, 4, 1, fp))
        return IT_READ_ERROR;
      if(strncmp(s.magic, "IMPS", 4))
        return IT_INVALID_SAMPLE;

      if(!fread(s.filename, 13, 1, fp))
        return IT_READ_ERROR;

      s.global_volume      = fgetc(fp);
      s.flags              = fgetc(fp);
      s.default_volume     = fgetc(fp);

      if(!fread(s.name, 26, 1, fp))
        return IT_READ_ERROR;
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
        return IT_READ_ERROR;

      if(s.global_volume < 0x40)
        m.uses[FT_SAMPLE_GLOBAL_VOLUME] = true;

      if(s.vibrato_depth)
        m.uses[FT_SAMPLE_VIBRATO] = true;

      if(s.flags & SAMPLE_COMPRESSED)
        m.uses[FT_SAMPLE_COMPRESSION] = true;
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
        O_("Warning : Failed to scan compressed sample %u\n", i);
    }
  }

  /* TODO load patterns. */

  O_("Name    : %s\n", h.name);
  O_("Version : T:%x TV:%03x V:%x\n", (h.tracker_version >> 12), (h.tracker_version & 0xFFF), h.format_version);
  O_("Orders  : %u\n", h.num_orders);
  O_("Patterns: %u\n", h.num_patterns);
  O_("Samples : %u\n", h.num_samples);
  if(h.flags & F_INST_MODE)
    O_("Instr.  : %u\n", h.num_instruments);
  O_("Uses    :");
  for(int i = 0; i < NUM_FEATURES; i++)
    if(m.uses[i])
      fprintf(stderr, " %s", FEATURE_STR[i]);
  fprintf(stderr, "\n");

  if(Config.dump_samples)
  {
    static const char PAD[] =
      "---------------------------------------------------------------------";

    O_("        :\n");
    O_("        : %-25s  %-13s : %-10s %-10s %-10s %-10s %-10s : %-10s GV  DV  DP  %-8s : VSp VDp VWf VRt :\n",
      "Name", "Filename",
      "Length", "LoopStart", "LoopEnd", "Sus.Start", "Sus.End",
      "C5 Speed", "Flags"
    );
    O_("        : %.40s : %.54s : %.31s : %.15s :\n", PAD, PAD, PAD, PAD);

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

      O_("Sam. %-3x: %-25s  %-13.13s : %-10u %-10u %-10u %-10u %-10u :"
        " %-10u %-2x  %-2x  %-2x  %-8s : %-2x  %-2x  %-2x  %-2x  :\n",
        i, s.name, s.filename,
        s.length, s.loop_start, s.loop_end, s.sustain_loop_start, s.sustain_loop_end,
        s.c5_speed, s.global_volume, s.default_volume, s.default_pan, flagstr,
        s.vibrato_speed, s.vibrato_depth, s.vibrato_waveform, s.vibrato_rate
      );
    }

    if(m.uses[FT_SAMPLE_COMPRESSION])
    {
      O_("        :\n");
      O_("        : Scan?  %-10s %-10s : %-10s %-10s %-10s :\n",
        "CmpBytes", "UncmpBytes",
        "Min.Block", "Min.Smpls.", "Max.Block"
      );
      O_("        : %.28s : %.32s :\n", PAD, PAD);
      for(unsigned int i = 0; i < h.num_samples; i++)
      {
        IT_sample &s = m.samples[i];
        if(!(s.flags & SAMPLE_COMPRESSED))
          continue;

        O_("Sam. %-3x: %-6s %-10u %-10u : %-10u %-10u %-10u :\n",
          i, s.scanned ? "pass" : "fail",
          s.compressed_bytes, s.uncompressed_bytes,
          s.smallest_block, s.smallest_block_samples, s.largest_block
        );
      }
    }
  }

  if(Config.dump_patterns)
  {
    // FIXME
  }

  return IT_SUCCESS;
}

static void check_it(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("File    : %s\n", filename);

    setvbuf(fp, NULL, _IOFBF, 2048);

    int err = IT_read(fp);
    if(err)
      O_("Error: %s\n\n", IT_strerror(err));
    else
      fprintf(stderr, "\n");

    fclose(fp);
  }
  else
    O_("Failed to open '%s'.\n\n", filename);
}


int main(int argc, char *argv[])
{
  if(!argv || argc < 2)
  {
    fprintf(stderr, "%s%s", USAGE, Config.COMMON_FLAGS);
    return 0;
  }

  if(!Config.init(&argc, argv))
    return -1;

  bool read_stdin = false;
  for(int i = 1; i < argc; i++)
  {
    if(!strcmp(argv[i], "-"))
    {
      if(!read_stdin)
      {
        char buffer[1024];
        while(fgets_safe(buffer, stdin))
          check_it(buffer);
        read_stdin = true;
      }
      continue;
    }
    check_it(argv[i]);
  }

  if(num_its)
    O_("Total ITs        : %d\n", num_its);
  return 0;
}
