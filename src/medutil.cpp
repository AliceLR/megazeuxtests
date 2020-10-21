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

#include "common.hpp"

static const char USAGE[] =
  "Dump information about OctaMED files.\n\n"
  "Usage:\n"
  "  medutil [filename.ext...]\n\n";

static const char MAGIC_MMD0[] = "MMD0";
static const char MAGIC_MMD1[] = "MMD1";
static const char MAGIC_MMD2[] = "MMD2";
static const char MAGIC_MMD3[] = "MMD3";

static int num_med;
static int num_mmd0;
static int num_mmd1;
static int num_mmd2;
static int num_mmd3;

enum MED_error
{
  MED_SUCCESS,
  MED_READ_ERROR,
  MED_SEEK_ERROR,
  MED_NOT_A_MED,
  MED_NOT_IMPLEMENTED
};

static const char *MED_strerror(int err)
{
  switch(err)
  {
    case MED_SUCCESS:         return "no error";
    case MED_READ_ERROR:      return "read error";
    case MED_SEEK_ERROR:      return "seek error";
    case MED_NOT_A_MED:       return "not a .MED";
    case MED_NOT_IMPLEMENTED: return "feature not implemented";
  }
  return "unknown error";
}

enum MED_features
{
  FT_CMD_SPEED_LO,
  FT_CMD_SPEED_HIGH,
  FT_CMD_TEMPO,
  FT_INST_SYNTH,
  FT_INST_HYBRID,
  NUM_FEATURES
};

static const char * const FEATURE_DESC[NUM_FEATURES] =
{
  "Cmd9<20",
  "Cmd9>20",
  "CmdFxx",
  "Synth",
  "Hyb",
};

struct MED_handler
{
  const char *magic;
  int (*read_fn)(FILE *fp);
};

static int read_mmd0(FILE *fp);
static int read_mmd1(FILE *fp);
static int read_mmd2(FILE *fp);
static int read_mmd3(FILE *fp);

static const MED_handler HANDLERS[] =
{
  { MAGIC_MMD0, read_mmd0 },
  { MAGIC_MMD1, read_mmd1 },
  { MAGIC_MMD2, read_mmd2 },
  { MAGIC_MMD3, read_mmd3 },
};

/**************** OctaMED MMD0 and MMD1 ******************/

enum MMD0instrtype
{
  MMD0_HYBRID  = -2,
  MMD0_SYNTH   = -1,
  MMD0_SAMPLE  = 0,
  MMD0_IFF5OCT = 1,
  MMD0_IFF3OCT = 2,
  MMD0_IFF2OCT = 3,
  MMD0_IFF4OCT = 4,
  MMD0_IFF6OCT = 5,
  MMD0_IFF7OCT = 6
};

enum MMD0flags
{
  F_FILTER_ON  = (1<<0),
  F_JUMPING_ON = (1<<1),
  F_JUMP_8TH   = (1<<2),
  F_INSTRSATT  = (1<<3), // ?
  F_VOLUME_HEX = (1<<4),
  F_MOD_SLIDES = (1<<5), // "ST/NT/PT compatible sliding"
  F_8_CHANNEL  = (1<<6), // "this is OctaMED 5-8 channel song"

  F2_BPM_MASK  = 0x1F,
  F2_BPM       = (1<<5), // BPM mode on.
};

enum MMD0effects
{
  E_ARPEGGIO         = 0x00,
  E_PORTAMENTO_UP    = 0x01,
  E_PORTAMENTO_DOWN  = 0x02,
  E_TONE_PORTAMENTO  = 0x03,
  E_VIBRATO          = 0x04,
  E_VIBRATO_OLD      = 0x05,
  E_SET_HOLD_DECAY   = 0x08,
  E_SPEED            = 0x09,
  E_VOLUME_SLIDE_MOD = 0x0A,
  E_POSITION_JUMP    = 0x0B,
  E_SET_VOLUME       = 0x0C,
  E_VOLUME_SLIDE     = 0x0D,
  E_SYNTH_JUMP       = 0x0E,
  E_TEMPO            = 0x0F,
};

struct MMD0sample
{
  /* 0 */ uint16_t repeat_start;  // Divided by 2.
  /* 2 */ uint16_t repeat_length; // Divided by 2.
  /* 4 */ uint8_t  midi_channel;  // 0: not MIDI.
  /* 5 */ uint8_t  midi_preset;
  /* 6 */ uint8_t  default_volume;
  /* 7 */ int8_t   transpose;
};

struct MMD0song
{
  /*   0 */ MMD0sample samples[63];
  /* 504 */ uint16_t   num_blocks;
  /* 506 */ uint16_t   num_orders;
  /* 508 */ uint8_t    orders[256];
  /* 764 */ uint16_t   default_tempo;
  /* 766 */ int8_t     transpose;
  /* 767 */ uint8_t    flags;
  /* 768 */ uint8_t    flags2;
  /* 769 */ uint8_t    tempo2;
  /* 770 */ uint8_t    track_volume[16];
  /* 786 */ uint8_t    song_volume;
  /* 787 */ uint8_t    num_instruments;
};

struct MMD1block
{
  uint16_t num_tracks;       // uint8_t for MMD0
  uint16_t num_rows;         // uint8_t for MMD0 (stored as length - 1 for both)
  uint32_t blockinfo_offset; // MMD1 only.

  // These are from the blockinfo struct.
  uint32_t  highlight_offset;
  uint32_t  block_name_offset;
  uint32_t  block_name_length;
  //uint32_t  reserved[6]; // just ignore these lol
};

struct MMD0instr
{
  /*   0 */ uint32_t length;
  /*   4 */ int16_t  type;
};

/* Following fields are only present for synth instruments. */
struct MMD0synth
{
  /*   6 */ uint8_t  default_decay;
  /*   7 */ uint8_t  reserved[3];
  /*  10 */ uint16_t hy_repeat_offset;
  /*  12 */ uint16_t hy_repeat_length;
  /*  14 */ uint16_t volume_table_len;
  /*  16 */ uint16_t waveform_table_len;
  /*  18 */ uint8_t  volume_table_speed;
  /*  19 */ uint8_t  waveform_table_speed;
  /*  20 */ uint16_t num_waveforms;
  /*  22 */ uint8_t  volume_table[128];
  /* 150 */ uint8_t  waveform_table[128];
  /* 278 */ uint32_t waveform_offsets[64]; /* struct SynthWF * */
};

struct MMD0head
{
  /*  0 */ char     magic[4];
  /*  4 */ uint32_t file_length;
  /*  8 */ uint32_t song_offset; /* struct MMD0song * */
  /* 12 */ uint32_t reserved0;
  /* 16 */ uint32_t block_array_offset; /* struct MMD0Block ** */
  /* 20 */ uint32_t reserved1;
  /* 24 */ uint32_t sample_array_offset; /* struct InstrHdr ** */
  /* 28 */ uint32_t reserved2;
  /* 32 */ uint32_t expansion_offset; /* struct MMD0exp * */
  /* 36 */ uint32_t reserved3;
  /* 40 */ uint16_t player_state; /* All of these can be ignored... */
  /* 42 */ uint16_t player_block;
  /* 44 */ uint16_t player_line;
  /* 46 */ uint16_t player_sequence;
  /* 48 */ int16_t  actplayline;
  /* 50 */ uint8_t  counter;
  /* 51 */ uint8_t  num_extra_songs;
};

struct MMD0note
{
  uint8_t note;
  uint8_t instrument;
  uint8_t effect;
  uint8_t param;

  void mmd0(int a, int b, int c)
  {
    note       = (a & 0x3F);
    instrument = ((a & 0x80) >> 3) | ((a & 0x40) >> 1) | ((b & 0xF0) >> 4); // WTF?
    effect     = (b & 0x0F);
    param      = c;
  }

  void mmd1(int a, int b, int c, int d)
  {
    note       = (a & 0x7F);
    instrument = (b & 0x3F);
    effect     = c;
    param      = d;
  }
};

struct MMD0
{
  MMD0head   header;
  MMD0song   song;
  MMD1block  patterns[256];
  MMD0instr  instruments[255];
  MMD0note  *pattern_data[256];
  MMD0synth *synth_data[255];
  uint32_t   pattern_offsets[256];
  uint32_t   instrument_offsets[255];
  bool       uses[NUM_FEATURES];

  ~MMD0()
  {
    for(int i = 0; i < arraysize(pattern_data); i++)
      delete[] pattern_data[i];
    for(int i = 0; i < arraysize(synth_data); i++)
      delete[] synth_data[i];
  }
};

static int read_mmd0_mmd1(FILE *fp, bool is_mmd1)
{
  MMD0 m{};
  MMD0head &h = m.header;
  MMD0song &s = m.song;

  /**
   * So many of these would need to be byte-reversed that
   * it's easier to just stream these values out (the pointer
   * packing in the synth struct breaks on modern machines anyway).
   */

  /**
   * Header.
   */
  if(!fread(h.magic, 4, 1, fp))
    return MED_READ_ERROR;

  h.file_length         = fget_u32be(fp);
  h.song_offset         = fget_u32be(fp);
  h.reserved0           = fget_u32be(fp);
  h.block_array_offset  = fget_u32be(fp);
  h.reserved1           = fget_u32be(fp);
  h.sample_array_offset = fget_u32be(fp);
  h.reserved2           = fget_u32be(fp);
  h.expansion_offset    = fget_u32be(fp);
  h.reserved3           = fget_u32be(fp);
  h.player_state        = fget_u16be(fp);
  h.player_line         = fget_u16be(fp);
  h.player_sequence     = fget_u16be(fp);
  h.actplayline         = fget_s16be(fp);
  h.counter             = fgetc(fp);
  h.num_extra_songs     = fgetc(fp);

  if(feof(fp))
    return MED_READ_ERROR;

  O_("Type      : %4.4s\n", h.magic);

  /**
   * Song.
   */
  if(fseek(fp, h.song_offset, SEEK_SET))
    return MED_SEEK_ERROR;

  for(int i = 0; i < 63; i++)
  {
    MMD0sample &sm = s.samples[i];

    sm.repeat_start   = fget_u16be(fp);
    sm.repeat_length  = fget_u16be(fp);
    sm.midi_channel   = fgetc(fp);
    sm.midi_preset    = fgetc(fp);
    sm.default_volume = fgetc(fp);
    sm.transpose      = fgetc(fp);
  }
  s.num_blocks      = fget_u16be(fp);
  s.num_orders      = fget_u16be(fp);

  if(!fread(s.orders, 256, 1, fp))
    return MED_READ_ERROR;

  s.default_tempo   = fget_u16be(fp);
  s.transpose       = fgetc(fp);
  s.flags           = fgetc(fp);
  s.flags2          = fgetc(fp);
  s.tempo2          = fgetc(fp);

  if(!fread(s.track_volume, 16, 1, fp))
    return MED_READ_ERROR;

  s.song_volume     = fgetc(fp);
  s.num_instruments = fgetc(fp);

  if(feof(fp))
    return MED_READ_ERROR;

  O_("# Blocks  : %u\n", s.num_blocks);
  O_("# Orders  : %u\n", s.num_orders);
  O_("# Instr.  : %u\n", s.num_instruments);

  if(s.flags2 & F2_BPM)
  {
    O_("BPM       : %u\n", s.default_tempo);
    O_("Beat rows : %u\n", (s.flags2 & F2_BPM_MASK) + 1);
    O_("Speed     : %u\n", s.tempo2);
  }
  else
  {
    O_("Tempo     : %u\n", s.default_tempo);
    O_("Speed     : %u\n", s.tempo2);
  }

  /**
   * Block array.
   */
  if(s.num_blocks > 256)
    return MED_NOT_IMPLEMENTED;

  if(fseek(fp, h.block_array_offset, SEEK_SET))
    return MED_SEEK_ERROR;

  for(size_t i = 0; i < s.num_blocks; i++)
    m.pattern_offsets[i] = fget_u32be(fp);

  /**
   * "Blocks" (aka patterns).
   */
  for(size_t i = 0; i < s.num_blocks; i++)
  {
    if(!m.pattern_offsets[i])
      continue;

    if(fseek(fp, m.pattern_offsets[i], SEEK_SET))
      return MED_SEEK_ERROR;

    MMD1block &b = m.patterns[i];

    if(is_mmd1)
    {
      b.num_tracks       = fget_u16be(fp);
      b.num_rows         = fget_u16be(fp) + 1;
      b.blockinfo_offset = fget_u32be(fp); // TODO handle this.
    }
    else
    {
      b.num_tracks = fgetc(fp);
      b.num_rows   = fgetc(fp) + 1;
    }

    MMD0note *pat = new MMD0note[b.num_tracks * b.num_rows];
    m.pattern_data[i] = pat;

    MMD0note *current = pat;

    for(size_t j = 0; j < b.num_rows; j++)
    {
      for(size_t k = 0; k < b.num_tracks; k++)
      {
        int a = fgetc(fp);
        int b = fgetc(fp);
        int c = fgetc(fp);

        if(is_mmd1)
        {
          int d = fgetc(fp);
          current->mmd1(a, b, c, d);
        }
        else
          current->mmd0(a, b, c);

        if(current->effect == E_SPEED)
        {
          if(current->param > 0x20)
            m.uses[FT_CMD_SPEED_HIGH] = true;
          else
            m.uses[FT_CMD_SPEED_LO] = true;
        }
        if(current->effect == E_TEMPO)
          m.uses[FT_CMD_TEMPO] = true;

        current++;
      }
    }
  }

  /**
   * Instruments.
   */

  /**
   * Extension data?
   */

  O_("Uses      :");
  for(int i = 0; i < NUM_FEATURES; i++)
    if(m.uses[i])
      fprintf(stderr, " %s", FEATURE_DESC[i]);
  fprintf(stderr, "\n");

  return MED_SUCCESS;
}

static int read_mmd0(FILE *fp)
{
  num_mmd0++;
  return read_mmd0_mmd1(fp, false);
}

static int read_mmd1(FILE *fp)
{
  num_mmd1++;
  return read_mmd0_mmd1(fp, true);
}

static int read_mmd2(FILE *fp)
{
  num_mmd2++;
  return MED_NOT_IMPLEMENTED;
}

static int read_mmd3(FILE *fp)
{
  num_mmd3++;
  return MED_NOT_IMPLEMENTED;
}

static int read_med(FILE *fp)
{
  char magic[4];
  if(!fread(magic, 4, 1, fp))
    return MED_READ_ERROR;

  rewind(fp);

  for(int i = 0; i < arraysize(HANDLERS); i++)
  {
    if(!memcmp(HANDLERS[i].magic, magic, 4))
    {
      num_med++;
      return HANDLERS[i].read_fn(fp);
    }
  }
  return MED_NOT_A_MED;
}

static void check_med(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    setvbuf(fp, NULL, _IOFBF, 2048);

    O_("File      : %s\n", filename);

    int err = read_med(fp);
    if(err)
      O_("Error     : %s\n\n", MED_strerror(err));
    else
      fprintf(stderr, "\n");

    fclose(fp);
  }
  else
    O_("Failed to open '%s'.\n\n", filename);
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
              check_med(buffer);

            read_stdin = true;
          }
          continue;

/*        case 'p':
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
          break;*/
      }
    }
    check_med(arg);
  }
  return 0;
}
