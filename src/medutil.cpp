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

static bool dump_samples;
static bool dump_patterns;
static bool dump_pattern_rows;

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
  FT_MULTIPLE_SONGS,
  FT_VARIABLE_TRACKS,
  FT_OCTAVES_8_AND_9,
  FT_TRANSPOSE_SONG,
  FT_TRANSPOSE_INSTRUMENT,
  FT_8_CHANNEL_MODE,
  FT_INIT_TEMPO_COMPAT,
  FT_BEAT_ROWS_NOT_4,
  FT_CMD_PORTAMENTO_VOLSLIDE,
  FT_CMD_VIBRATO_VOLSLIDE,
  FT_CMD_TREMOLO,
  FT_CMD_HOLD_DECAY,
  FT_CMD_SPEED_DEFAULT,
  FT_CMD_SPEED_LO,
  FT_CMD_SPEED_HIGH,
  FT_CMD_BREAK,
  FT_CMD_PLAY_TWICE,
  FT_CMD_PLAY_TWICE_NO_NOTE,
  FT_CMD_PLAY_DELAY,
  FT_CMD_PLAY_THREE_TIMES,
  FT_CMD_PLAY_THREE_TIMES_NO_NOTE,
  FT_CMD_SET_PITCH,
  FT_CMD_STOP_PLAYING,
  FT_CMD_STOP_NOTE,
  FT_CMD_TEMPO_COMPAT,
  FT_CMD_TEMPO,
  FT_CMD_BPM_BUGGY,
  FT_CMD_BPM_LO,
  FT_CMD_BPM,
  FT_CMD_FINE_PORTAMENTO,
  FT_CMD_PT_VIBRATO,
  FT_CMD_FINETUNE,
  FT_CMD_LOOP,
  FT_CMD_LOOP_OVER_0F,
  FT_CMD_18_STOP,
  FT_CMD_18_STOP_OVER_0F,
  FT_CMD_OFFSET,
  FT_CMD_FINE_VOLUME,
  FT_CMD_1D_BREAK,
  FT_CMD_PATTERN_DELAY,
  FT_CMD_PATTERN_DELAY_OVER_0F,
  FT_CMD_1F_DELAY,
  FT_CMD_1F_RETRIGGER,
  FT_CMD_1F_DELAY_RETRIGGER,
  FT_INST_MIDI,
  FT_INST_IFFOCT,
  FT_INST_SYNTH,
  FT_INST_SYNTH_HYBRID,
  FT_INST_EXT,
  FT_INST_HOLD_DECAY,
  FT_INST_DEFAULT_PITCH,
  NUM_FEATURES
};

static const char * const FEATURE_DESC[NUM_FEATURES] =
{
  ">1Songs",
  "VarTracks",
  "Oct8/9",
  "STrans",
  "ITrans",
  "8ChMode",
  "Tempo<=0A",
  "BRows!=4",
  "CmPortVol",
  "CmVibVol",
  "CmTremolo",
  "CmHoldDecay",
  "Cm900",
  "Cm9<=20",
  "Cm9>20",
  "CmFBrk",
  "CmFTwice",
  "CmFF1NoNote",
  "CmFDelay",
  "CmFThree",
  "CmFF3NoNote",
  "CmFPitch",
  "CmFStop",
  "CmFOff",
  "CmF<=0A",
  "CmF>0A",
  "CmFBPM<=2",
  "CmFBPM<=20",
  "CmFBPM",
  "CmFinePort",
  "CmPTVib",
  "CmFinetune",
  "CmLoop",
  "CmLoop>0F",
  "Cm18Stop",
  "Cm18Stop>0F",
  "CmOffset",
  "CmFineVol",
  "Cm1DBrk",
  "CmPatDelay",
  "CmPatDelay>0F",
  "Cm1FDelay",
  "Cm1FRetrg",
  "Cm1FBoth",
  "MIDI",
  "IFFOct",
  "Synth",
  "Hybrid",
  "ExtSample",
  "HoldDecay",
  "DefPitch",
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
  I_IFF7OCT = 6,
  I_EXT     = 7
};

static const char *MED_insttype_str(int t)
{
  switch(t)
  {
    case I_HYBRID:  return "Hyb";
    case I_SYNTH:   return "Syn";
    case I_SAMPLE:  return "Smp";
    case I_IFF5OCT: return "IO5";
    case I_IFF3OCT: return "IO3";
    case I_IFF2OCT: return "IO2";
    case I_IFF4OCT: return "IO4";
    case I_IFF6OCT: return "IO6";
    case I_IFF7OCT: return "IO7";
    case I_EXT:     return "Ext";
  }
  return "???";
}

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
  E_PORTA_VOLSLIDE   = 0x05,
  E_VIBRATO_VOLSLIDE = 0x06,
  E_TREMOLO          = 0x07,
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

/* Extra instrument data. */
struct MMD3instr_ext
{
  /* <V5: 4 bytes */

  /*   0 */ uint8_t    hold;
  /*   1 */ uint8_t    decay;
  /*   2 */ uint8_t    suppress_midi_off;
  /*   3 */ int8_t     finetune;

  /* V5: 8 bytes */

  /*   4 */ uint8_t    default_pitch;
  /*   5 */ uint8_t    instrument_flags;
  /*   6 */ uint16_t   long_midi_preset;

  /* V5.02: 10 bytes */

  /*   8 */ uint8_t    output_device;
  /*   9 */ uint8_t    reserved;

  /* V7: 18 bytes */

  /*  10 */ uint32_t   long_repeat_start;
  /*  14 */ uint32_t   long_repeat_length;
};

/* Instrument names. */
struct MMD3instr_info
{
  char name[41]; /* Is stored as 40. */
};

struct MMD3exp
{
  /*   0 */ uint32_t   nextmod_offset; /* struct MMD0 * */
  /*   4 */ uint32_t   sample_ext_offset; /* struct InstrExt * */
  /*   8 */ uint16_t   sample_ext_entries;
  /*  10 */ uint16_t   sample_ext_size;
  /*  12 */ uint32_t   annotation_offset;
  /*  16 */ uint32_t   annotation_length;
  /*  20 */ uint32_t   instr_info_offset; /* struct MMDInstrInfo * */
  /*  24 */ uint16_t   instr_info_entries;
  /*  26 */ uint16_t   instr_info_size;
  /*  28 */ uint32_t   jumpmask; /* ? */
  /*  32 */ uint32_t   rgbtable_offset;
  /*  36 */ uint32_t   channel_split;
  /*  40 */ uint32_t   notation_info_offset; /* struct NotationInfo * */
  /*  44 */ uint32_t   songname_offset;
  /*  48 */ uint32_t   songname_length;
  /*  52 */ uint32_t   dumps_offset; /* struct MMDDumpData * */
  /*  56 */ uint32_t   mmdinfo_offset; /* struct MMDInfo * */
  /*  60 */ uint32_t   mmdrexx_offset; /* struct MMDARexx * */
  /*  64 */ uint32_t   mmdcmd3x_offset; /* struct MMDMIDICmd3x * */
  /*  68 */ uint32_t   reserved[3];
  /*  80 */ uint32_t   tag_end; /* ? */
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
  MMD0head       header;
  MMD0song       song;
  MMD3exp        exp;
  MMD1block      patterns[MAX_BLOCKS];
  MMD0instr      instruments[MAX_INSTRUMENTS];
  MMD3instr_ext  instruments_ext[MAX_INSTRUMENTS];
  MMD3instr_info instruments_info[MAX_INSTRUMENTS];
  MMD0note      *pattern_data[MAX_BLOCKS];
  MMD0synth     *synth_data[MAX_INSTRUMENTS];
  uint32_t      *pattern_highlight[MAX_BLOCKS];
  uint32_t       pattern_offsets[MAX_BLOCKS];
  uint32_t       instrument_offsets[MAX_INSTRUMENTS];
  uint32_t       num_tracks;
  bool           uses[NUM_FEATURES];

  ~MMD0()
  {
    for(int i = 0; i < arraysize(pattern_data); i++)
    {
      delete[] pattern_data[i];
      delete[] pattern_highlight[i];
    }
    for(int i = 0; i < arraysize(synth_data); i++)
      delete synth_data[i];
  }

  bool highlight(int pattern, int row)
  {
    if(pattern_highlight[pattern])
    {
      uint32_t t = pattern_highlight[pattern][row / 32];
      uint32_t m = 1 << (row & 31);
      return (t & m) != 0;
    }
    return false;
  }
};

static int read_mmd0_mmd1(FILE *fp, bool is_mmd1)
{
  MMD0 m{};
  MMD0head &h = m.header;
  MMD0song &s = m.song;
  MMD3exp  &x = m.exp;

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

    if(sm.midi_channel > 0)
      m.uses[FT_INST_MIDI] = true;

    if(sm.transpose != 0)
      m.uses[FT_TRANSPOSE_INSTRUMENT] = true;
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

  if(s.transpose != 0)
    m.uses[FT_TRANSPOSE_SONG] = true;

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
      b.blockinfo_offset = fget_u32be(fp);
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

    bool is_bpm_mode = (s.flags2 & F2_BPM);
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

        /**
         * C-1=1, C#1=2... + 7 octaves.
         * Some songs actually rely on these high octaves playing very
         * low tones (see "childplay.med" by Blockhead).
         */
        if(current->note >= (1 + 12 * 7))
          m.uses[FT_OCTAVES_8_AND_9] = true;

        switch(current->effect)
        {
          case E_PORTA_VOLSLIDE:
            m.uses[FT_CMD_PORTAMENTO_VOLSLIDE] = true;
            break;
          case E_VIBRATO_VOLSLIDE:
            m.uses[FT_CMD_VIBRATO_VOLSLIDE] = true;
            break;
          case E_TREMOLO:
            m.uses[FT_CMD_TREMOLO] = true;
            break;
          case E_SET_HOLD_DECAY:
            m.uses[FT_CMD_HOLD_DECAY] = true;
            break;

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
                if(!current->note)
                  m.uses[FT_CMD_PLAY_TWICE_NO_NOTE] = true;
                m.uses[FT_CMD_PLAY_TWICE] = true;
                break;
              case 0xF2:
                m.uses[FT_CMD_PLAY_DELAY] = true;
                break;
              case 0xF3:
                if(!current->note)
                  m.uses[FT_CMD_PLAY_THREE_TIMES_NO_NOTE] = true;
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
                if(!is_bpm_mode)
                {
                  if(current->param <= 0x0A)
                    m.uses[FT_CMD_TEMPO_COMPAT] = true;
                  else
                    m.uses[FT_CMD_TEMPO] = true;
                }
                else
                {
                  /**
                   * OctaMED has a weird bug with these BPMs where they will
                   * cause it to play at tempo 33 and ignore the rows per beat.
                   * Some tracks actually use this and rely on it!
                   */
                  if(current->param <= 0x02)
                    m.uses[FT_CMD_BPM_BUGGY] = true;
                  else
                  // BPMs in this range had a BPM mode bug in MikMod...
                  if(current->param <= 0x20)
                    m.uses[FT_CMD_BPM_LO] = true;
                  else
                    m.uses[FT_CMD_BPM] = true;
                }
                break;
            }
            break;
          }

          case E_FINE_PORTA_UP:
          case E_FINE_PORTA_DOWN:
            m.uses[FT_CMD_FINE_PORTAMENTO] = true;
            break;
          case E_VIBRATO_COMPAT:
            m.uses[FT_CMD_PT_VIBRATO] = true;
            break;
          case E_FINETUNE:
            m.uses[FT_CMD_FINETUNE] = true;
            break;
          case E_LOOP:
            if(current->param > 0x0F)
              m.uses[FT_CMD_LOOP_OVER_0F] = true;
            m.uses[FT_CMD_LOOP] = true;
            break;
          case E_STOP_NOTE:
            if(current->param > 0x0F)
              m.uses[FT_CMD_18_STOP_OVER_0F] = true;
            m.uses[FT_CMD_18_STOP] = true;
            break;
          case E_SAMPLE_OFFSET:
            m.uses[FT_CMD_OFFSET] = true;
            break;
          case E_FINE_VOLUME_UP:
          case E_FINE_VOLUME_DOWN:
            m.uses[FT_CMD_FINE_VOLUME] = true;
            break;
          case E_PATTERN_BREAK:
            m.uses[FT_CMD_1D_BREAK] = true;
            break;
          case E_PATTERN_DELAY:
            if(current->param > 0x0F)
              m.uses[FT_CMD_PATTERN_DELAY_OVER_0F] = true;
            m.uses[FT_CMD_PATTERN_DELAY] = true;
            break;
          case E_DELAY_RETRIGGER:
            bool uses_delay     = !!(current->param & 0xF0);
            bool uses_retrigger = !!(current->param & 0x0F);
            if(uses_delay && uses_retrigger)
              m.uses[FT_CMD_1F_DELAY_RETRIGGER] = true;
            else
            if(uses_delay)
              m.uses[FT_CMD_1F_DELAY] = true;
            else
            if(uses_retrigger)
              m.uses[FT_CMD_1F_RETRIGGER] = true;
            break;
        }

        current++;
      }
    }

    /* Dumping patterns? Might as well get the highlighting too. */
    if(dump_pattern_rows && b.blockinfo_offset)
    {
      if(fseek(fp, b.blockinfo_offset, SEEK_SET))
        return MED_SEEK_ERROR;

      b.highlight_offset  = fget_u32be(fp);
      b.block_name_offset = fget_u32be(fp);
      b.block_name_length = fget_u32be(fp);
      /* Several reserved words here... */

      if(b.highlight_offset)
      {
        if(fseek(fp, b.highlight_offset, SEEK_SET))
          return MED_SEEK_ERROR;

        uint32_t highlight_len = (b.num_rows + 31)/32;
        uint32_t *highlight = new uint32_t[highlight_len];
        m.pattern_highlight[i] = highlight;

        for(size_t j = 0; j < highlight_len; j++)
          highlight[j] = fget_u32be(fp);
      }
    }
  }

  /* Do a quick check for blocks with fewer tracks than the maximum track count. */
  for(size_t i = 0; i < s.num_blocks; i++)
  {
    if(m.patterns[i].num_tracks < m.num_tracks)
    {
      m.uses[FT_VARIABLE_TRACKS] = true;
      break;
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

      if(inst.type == I_HYBRID)
        m.uses[FT_INST_SYNTH_HYBRID] = true;
      else
        m.uses[FT_INST_SYNTH] = true;
    }
    else

    if(inst.type > 0)
      m.uses[FT_INST_IFFOCT] = true;
  }

  if(feof(fp))
    return MED_READ_ERROR;

  /**
   * Expansion data.
   */
  if(h.expansion_offset && !fseek(fp, h.expansion_offset, SEEK_SET))
  {
    x.nextmod_offset       = fget_u32be(fp);
    x.sample_ext_offset    = fget_u32be(fp);
    x.sample_ext_entries   = fget_u16be(fp);
    x.sample_ext_size      = fget_u16be(fp);
    x.annotation_offset    = fget_u32be(fp);
    x.annotation_length    = fget_u32be(fp);
    x.instr_info_offset    = fget_u32be(fp);
    x.instr_info_entries   = fget_u16be(fp);
    x.instr_info_size      = fget_u16be(fp);
    x.jumpmask             = fget_u32be(fp);
    x.rgbtable_offset      = fget_u32be(fp);
    x.channel_split        = fget_u32be(fp);
    x.notation_info_offset = fget_u32be(fp);
    x.songname_offset      = fget_u32be(fp);
    x.songname_length      = fget_u32be(fp);
    x.dumps_offset         = fget_u32be(fp);
    x.mmdinfo_offset       = fget_u32be(fp);
    x.mmdrexx_offset       = fget_u32be(fp);
    x.reserved[0]          = fget_u32be(fp);
    x.reserved[1]          = fget_u32be(fp);
    x.reserved[2]          = fget_u32be(fp);
    x.tag_end              = fget_u32be(fp);

    if(feof(fp))
      return MED_READ_ERROR;

    if(x.sample_ext_entries > MAX_INSTRUMENTS)
      return MED_TOO_MANY_INSTR;

    if(x.sample_ext_entries && fseek(fp, x.sample_ext_offset, SEEK_SET))
      return MED_SEEK_ERROR;

    for(size_t i = 0; i < x.sample_ext_entries; i++)
    {
      MMD3instr_ext &sx = m.instruments_ext[i];
      int skip = x.sample_ext_size;

      if(x.sample_ext_size >= 4)
      {
        sx.hold               = fgetc(fp);
        sx.decay              = fgetc(fp);
        sx.suppress_midi_off  = fgetc(fp);
        sx.finetune           = fgetc(fp);
        skip -= 4;
      }
      if(x.sample_ext_size >= 8)
      {
        sx.default_pitch      = fgetc(fp);
        sx.instrument_flags   = fgetc(fp);
        sx.long_midi_preset   = fget_u16be(fp);
        skip -= 4;
      }
      if(x.sample_ext_size >= 10)
      {
        sx.output_device      = fgetc(fp);
        sx.reserved           = fgetc(fp);
        skip -= 2;
      }
      if(x.sample_ext_size >= 18)
      {
        sx.long_repeat_start  = fget_u32be(fp);
        sx.long_repeat_length = fget_u32be(fp);
        skip -= 8;
      }

      if(skip && fseek(fp, skip, SEEK_CUR))
        return MED_SEEK_ERROR;

      if(sx.hold)
        m.uses[FT_INST_HOLD_DECAY] = true;
      if(sx.default_pitch)
        m.uses[FT_INST_DEFAULT_PITCH] = true;
    }

    if(x.instr_info_entries > MAX_INSTRUMENTS)
      return MED_TOO_MANY_INSTR;

    if(x.instr_info_entries && fseek(fp, x.instr_info_offset, SEEK_SET))
      return MED_SEEK_ERROR;

    for(size_t i = 0; i < x.instr_info_entries; i++)
    {
      MMD3instr_info &sxi = m.instruments_info[i];
      int skip = x.instr_info_size;

      if(x.instr_info_size >= 40)
      {
        fread(sxi.name, 40, 1, fp);
        sxi.name[40] = '\0';
        skip -= 40;
      }
      if(skip && fseek(fp, skip, SEEK_CUR))
        return MED_SEEK_ERROR;
    }
  }

  if(s.flags & F_8_CHANNEL)
    m.uses[FT_8_CHANNEL_MODE] = true;

  if(h.num_extra_songs && x.nextmod_offset)
    m.uses[FT_MULTIPLE_SONGS] = true;

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

    if(s.default_tempo >= 0x01 && s.default_tempo <= 0x0A)
      m.uses[FT_INIT_TEMPO_COMPAT] = true;
  }

  O_("Uses      :");
  for(int i = 0; i < NUM_FEATURES; i++)
    if(m.uses[i])
      fprintf(stderr, " %s", FEATURE_DESC[i]);
  fprintf(stderr, "\n");

  if(dump_samples)
  {
    O_("          :\n");
    O_("          : Type  Length      Loop Start  Loop Len.  : MIDI       : Vol  Tr. : Hold/Decay Fine : Name\n");
    O_("          : ----  ----------  ----------  ---------- : ---  ----- : ---  --- : ---  ---   ---  : ----\n");
    for(unsigned int i = 0; i < s.num_instruments; i++)
    {
      MMD0sample     &sm = s.samples[i];
      MMD0instr      &si = m.instruments[i];
      //MMD0synth      *ss = m.synths[i];
      MMD3instr_ext  &sx = m.instruments_ext[i];
      MMD3instr_info &sxi = m.instruments_info[i];

      unsigned int length         = si.length;
      unsigned int repeat_start   = sx.long_repeat_start ? sx.long_repeat_start : sm.repeat_start * 2;
      unsigned int repeat_length  = sx.long_repeat_length ? sx.long_repeat_length : sm.repeat_length * 2;
      unsigned int midi_preset    = sx.long_midi_preset ? sx.long_midi_preset : sm.midi_preset;
      unsigned int midi_channel   = sm.midi_channel;
      unsigned int default_volume = sm.default_volume;
      int transpose = sm.transpose;

      unsigned int hold  = sx.hold;
      unsigned int decay = sx.decay;
      int finetune = sx.finetune;

      O_("Sample %02x : %-4.4s  %-10u  %-10u  %-10u : %-3u  %-5u : %-3u  %-3d : %-3u  %-3u   %-3d  : %s\n",
        i, MED_insttype_str(si.type), length, repeat_start, repeat_length,
        midi_channel, midi_preset, default_volume, transpose, hold, decay, finetune, sxi.name
      );
    }
  }

  if(dump_patterns)
  {
    O_("          :\n");
    O_(" Sequence :");
    for(size_t i = 0; i < s.num_orders; i++)
      fprintf(stderr, " %02x", s.orders[i]);
    fprintf(stderr, "\n");

    for(unsigned int i = 0; i < s.num_blocks; i++)
    {
      MMD1block &b = m.patterns[i];
      MMD0note *data = m.pattern_data[i];

      fprintf(stderr, "\n: Pattern %02x (%u rows, %u tracks)\n", i, b.num_rows, b.num_tracks);

      if(!dump_pattern_rows)
        continue;

      uint8_t p_note[256]{};
      uint8_t p_inst[256]{};
      uint8_t p_eff[256]{};
      int p_sz[256]{};
      bool print_pattern = false;

      /* Do a quick scan of the block to see how much info to print... */
      MMD0note *current = data;
      for(unsigned int row = 0; row < b.num_rows; row++)
      {
        for(unsigned int track = 0; track < b.num_tracks; track++, current++)
        {
          p_note[track] |= current->note != 0;
          p_inst[track] |= current->instrument != 0;
          p_eff[track]  |= (current->effect != 0) || (current->param != 0);

          p_sz[track] = (p_note[track] * 3) + (p_inst[track] * 3) + (p_eff[track] * 6);
          print_pattern |= (p_sz[track] > 0);
        }
      }

      if(!print_pattern)
      {
        O_("Pattern is blank.\n");
        continue;
      }

      O_("");
      for(unsigned int track = 0; track < b.num_tracks; track++)
        if(p_sz[track])
          fprintf(stderr, " %02x%*s:", track, p_sz[track] - 2, "");
      fprintf(stderr, "\n");

      O_("");
      for(unsigned int track = 0; track < b.num_tracks; track++)
        if(p_sz[track])
          fprintf(stderr, "%.*s:", p_sz[track] + 1, "-------------");
      fprintf(stderr, "\n");

      current = data;
      for(unsigned int row = 0; row < b.num_rows; row++)
      {
        fprintf(stderr, m.highlight(i, row) ? "X" : ":");

        for(unsigned int track = 0; track < b.num_tracks; track++, current++)
        {
          if(!p_sz[track])
            continue;

          if(p_note[track])
            fprintf(stderr, " %02x", current->note);
          if(p_inst[track])
            fprintf(stderr, " %02x", current->instrument);
          if(p_eff[track])
            fprintf(stderr, " %02x %02x", current->effect, current->param);
          fprintf(stderr, " :");
        }
        fprintf(stderr, "\n");
      }
    }
  }

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
