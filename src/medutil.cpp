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
static int num_unknown;

static const int MAX_BLOCKS      = 256;
static const int MAX_INSTRUMENTS = 63;

enum MED_error
{
  MED_SUCCESS,
  MED_READ_ERROR,
  MED_SEEK_ERROR,
  MED_NOT_A_MED,
  MED_NOT_IMPLEMENTED,
  MED_TOO_MANY_BLOCKS,
  MED_TOO_MANY_INSTR,
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
    case MED_TOO_MANY_BLOCKS: return "only <=256 blocks supported";
    case MED_TOO_MANY_INSTR:  return "only <=63 instruments supported";
  }
  return "unknown error";
}

enum MED_features
{
  FT_BEAT_ROWS_NOT_4,
  FT_CMD_SPEED_DEFAULT,
  FT_CMD_SPEED_LO,
  FT_CMD_SPEED_HIGH,
  FT_CMD_BREAK,
  FT_CMD_PLAY_TWICE,
  FT_CMD_PLAY_DELAY,
  FT_CMD_PLAY_THREE_TIMES,
  FT_CMD_SET_PITCH,
  FT_CMD_STOP_PLAYING,
  FT_CMD_STOP_NOTE,
  FT_CMD_TEMPO,
  FT_CMD_FINE_PORTAMENTO,
  FT_CMD_14,
  FT_CMD_FINETUNE,
  FT_CMD_LOOP,
  FT_CMD_18,
  FT_CMD_OFFSET,
  FT_CMD_FINE_VOLUME,
  FT_CMD_1D,
  FT_CMD_PATTERN_DELAY,
  FT_CMD_DELAY_RETRIGGER,
  FT_INST_SYNTH,
  NUM_FEATURES
};

static const char * const FEATURE_DESC[NUM_FEATURES] =
{
  "BRows!=4",
  "Cm900",
  "Cm9<20",
  "Cm9>20",
  "CmFBrk",
  "CmFTwice",
  "CmFDelay",
  "CmFThree",
  "CmFPitch",
  "CmFStop",
  "CmFOff",
  "CmFxx",
  "CmFinePort",
  "Cm14",
  "CmFinetune",
  "CmLoop",
  "Cm18",
  "CmOffset",
  "CmFineVol",
  "Cm1D",
  "CmPatDelay",
  "Cm1F",
  "Synth",
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
  I_HYBRID  = -2,
  I_SYNTH   = -1,
  I_SAMPLE  = 0,
  I_IFF5OCT = 1,
  I_IFF3OCT = 2,
  I_IFF2OCT = 3,
  I_IFF4OCT = 4,
  I_IFF6OCT = 5,
  I_IFF7OCT = 6
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
  E_FINE_PORTA_UP    = 0x11,
  E_FINE_PORTA_DOWN  = 0x12,
  E_VIBRATO_COMPAT   = 0x14,
  E_FINETUNE         = 0x15,
  E_LOOP             = 0x16,
  E_STOP_NOTE        = 0x18,
  E_SAMPLE_OFFSET    = 0x19,
  E_FINE_VOLUME_UP   = 0x1A,
  E_FINE_VOLUME_DOWN = 0x1B,
  E_PATTERN_BREAK    = 0x1D,
  E_PATTERN_DELAY    = 0x1E,
  E_DELAY_RETRIGGER  = 0x1F,
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
  /*  14 */ uint16_t volume_table_length;
  /*  16 */ uint16_t waveform_table_length;
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
  MMD1block  patterns[MAX_BLOCKS];
  MMD0instr  instruments[MAX_INSTRUMENTS];
  MMD0note  *pattern_data[MAX_BLOCKS];
  MMD0synth *synth_data[MAX_INSTRUMENTS];
  uint32_t   pattern_offsets[MAX_BLOCKS];
  uint32_t   instrument_offsets[MAX_INSTRUMENTS];
  uint32_t   num_tracks;
  bool       uses[NUM_FEATURES];

  ~MMD0()
  {
    for(int i = 0; i < arraysize(pattern_data); i++)
      delete[] pattern_data[i];
    for(int i = 0; i < arraysize(synth_data); i++)
      delete synth_data[i];
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

  /**
   * Block array.
   */
  if(s.num_blocks > MAX_BLOCKS)
    return MED_TOO_MANY_BLOCKS;

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

    if(m.num_tracks < b.num_tracks)
      m.num_tracks = b.num_tracks;

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

        switch(current->effect)
        {
          case E_SPEED:
          {
            if(current->param > 0x20)
              m.uses[FT_CMD_SPEED_HIGH] = true;
            else
            if(current->param > 0x00)
              m.uses[FT_CMD_SPEED_LO] = true;
            else
              m.uses[FT_CMD_SPEED_DEFAULT] = true;
            break;
          }

          case E_TEMPO:
          {
            switch(current->param)
            {
              case 0x00:
                m.uses[FT_CMD_BREAK] = true;
                break;
              case 0xF1:
                m.uses[FT_CMD_PLAY_TWICE] = true;
                break;
              case 0xF2:
                m.uses[FT_CMD_PLAY_DELAY] = true;
                break;
              case 0xF3:
                m.uses[FT_CMD_PLAY_THREE_TIMES] = true;
                break;
              case 0xF8: // Filter off
              case 0xF9: // Filter on
              case 0xFA: // Hold pedal on
              case 0xFB: // Hold pedal off
                break;
              case 0xFD:
                m.uses[FT_CMD_SET_PITCH] = true;
                break;
              case 0xFE:
                m.uses[FT_CMD_STOP_PLAYING] = true;
                break;
              case 0xFF:
                m.uses[FT_CMD_STOP_NOTE] = true;
                break;
              default:
                m.uses[FT_CMD_TEMPO] = true;
                break;
            }
            break;
          }

          case E_FINE_PORTA_UP:
          case E_FINE_PORTA_DOWN:
            m.uses[FT_CMD_FINE_PORTAMENTO] = true;
            break;
          case E_VIBRATO_COMPAT:
            m.uses[FT_CMD_14] = true;
            break;
          case E_FINETUNE:
            m.uses[FT_CMD_FINETUNE] = true;
            break;
          case E_LOOP:
            m.uses[FT_CMD_LOOP] = true;
            break;
          case E_STOP_NOTE:
            m.uses[FT_CMD_18] = true;
            break;
          case E_SAMPLE_OFFSET:
            m.uses[FT_CMD_OFFSET] = true;
            break;
          case E_FINE_VOLUME_UP:
          case E_FINE_VOLUME_DOWN:
            m.uses[FT_CMD_FINE_VOLUME] = true;
            break;
          case E_PATTERN_BREAK:
            m.uses[FT_CMD_1D] = true;
            break;
          case E_PATTERN_DELAY:
            m.uses[FT_CMD_PATTERN_DELAY] = true;
            break;
          case E_DELAY_RETRIGGER:
            m.uses[FT_CMD_DELAY_RETRIGGER] = true;
            break;
        }

        current++;
      }
    }
  }

  /**
   * Instruments array.
   */
  if(s.num_instruments > MAX_INSTRUMENTS)
    return MED_TOO_MANY_INSTR;

  if(fseek(fp, h.sample_array_offset, SEEK_SET))
    return MED_SEEK_ERROR;

  for(size_t i = 0; i < s.num_instruments; i++)
    m.instrument_offsets[i] = fget_u32be(fp);

  if(feof(fp))
    return MED_READ_ERROR;

  /**
   * Instruments.
   */
  for(size_t i = 0; i < s.num_instruments; i++)
  {
    if(!m.instrument_offsets[i])
      continue;

    if(fseek(fp, m.instrument_offsets[i], SEEK_SET))
      return MED_SEEK_ERROR;

    MMD0instr &inst = m.instruments[i];
    inst.length = fget_u32be(fp);
    inst.type   = fget_s16be(fp);

    if(inst.type == I_HYBRID || inst.type == I_SYNTH)
    {
      MMD0synth *syn = new MMD0synth;

      syn->default_decay         = fgetc(fp);
      syn->reserved[0]           = fgetc(fp);
      syn->reserved[1]           = fgetc(fp);
      syn->reserved[2]           = fgetc(fp);
      syn->hy_repeat_offset      = fget_u16be(fp);
      syn->hy_repeat_length      = fget_u16be(fp);
      syn->volume_table_length   = fget_u16be(fp);
      syn->waveform_table_length = fget_u16be(fp);
      syn->volume_table_speed    = fgetc(fp);
      syn->waveform_table_speed  = fgetc(fp);
      syn->num_waveforms         = fget_u16be(fp);

      if(!fread(syn->volume_table, 128, 1, fp) ||
       !fread(syn->waveform_table, 128, 1, fp))
        return MED_READ_ERROR;

      for(int j = 0; j < 64; j++)
        syn->waveform_offsets[j] = fget_u32be(fp);

      m.uses[FT_INST_SYNTH] = true;
    }
  }

  /**
   * Extension data?
   */

  O_("Type      : %4.4s\n", h.magic);
  O_("Size      : %u\n", h.file_length);
  O_("# Tracks  : %u\n", m.num_tracks);
  O_("# Blocks  : %u\n", s.num_blocks);
  O_("# Orders  : %u\n", s.num_orders);
  O_("# Instr.  : %u\n", s.num_instruments);

  if(s.flags2 & F2_BPM)
  {
    uint8_t beat_rows = (s.flags2 & F2_BPM_MASK) + 1;

    O_("BPM       : %u\n", s.default_tempo);
    O_("Beat rows : %u\n", beat_rows);
    O_("Speed     : %u\n", s.tempo2);

    if(beat_rows != 4)
      m.uses[FT_BEAT_ROWS_NOT_4] = true;
  }
  else
  {
    O_("Tempo     : %u\n", s.default_tempo);
    O_("Speed     : %u\n", s.tempo2);
  }

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
  num_unknown++;
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
  if(num_med)
    O_("Total .MED modules : %d\n", num_med);
  if(num_mmd0)
    O_("Total MMD0         : %d\n", num_mmd0);
  if(num_mmd1)
    O_("Total MMD1         : %d\n", num_mmd1);
  if(num_mmd2)
    O_("Total MMD2         : %d\n", num_mmd2);
  if(num_mmd3)
    O_("Total MMD3         : %d\n", num_mmd3);
  if(num_unknown)
    O_("Total unknown      : %d\n", num_unknown);
  return 0;
}
