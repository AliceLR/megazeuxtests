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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <vector>

#include "modutil.hpp"

static const char MAGIC_MED2[] = "MED\x02";
static const char MAGIC_MED3[] = "MED\x03";
static const char MAGIC_MED4[] = "MED\x04";
static const char MAGIC_MMD0[] = "MMD0";
static const char MAGIC_MMD1[] = "MMD1";
static const char MAGIC_MMD2[] = "MMD2";
static const char MAGIC_MMD3[] = "MMD3";
static const char MAGIC_MMDC[] = "MMDC";

static int num_med;
static int num_med2;
static int num_med3;
static int num_med4;
static int num_mmd0;
static int num_mmd1;
static int num_mmd2;
static int num_mmd3;
static int num_mmdc;

static const int MAX_BLOCKS      = 256;
static const int MAX_INSTRUMENTS = 63;
static const int MAX_WAVEFORMS   = 64;

/* MMDC is effectively MMD0 but with packed pattern data. */
static const int MMDC_VERSION    = -1;


enum MED_features
{
  FT_MULTIPLE_SONGS,
  FT_VARIABLE_TRACKS,
  FT_OVER_256_ROWS,
  FT_NOTE_HOLD,
  FT_NOTE_1,
  FT_OCTAVE_4,
  FT_OCTAVE_8,
  FT_TRANSPOSE_SONG,
  FT_TRANSPOSE_INSTRUMENT,
  FT_8_CHANNEL_MODE,
  FT_INIT_TEMPO_COMPAT,
  FT_BEAT_ROWS_NOT_4,
  FT_FILTER_ON,
  FT_MOD_SLIDES,
  FT_TICK_0_SLIDES,
  FT_COMMAND_PAGES,
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
  FT_CMD_DELAY_ONE_THIRD,
  FT_CMD_DELAY_TWO_THIRDS,
  FT_CMD_FILTER,
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
  FT_CMD_20_REVERSE,
  FT_CMD_20_RELATIVE_OFFSET,
  FT_CMD_LINEAR_PORTAMENTO,
  FT_CMD_TRACK_PANNING,
  FT_CMD_2F_ECHO_DEPTH,
  FT_CMD_2F_STEREO_SEPARATION,
  FT_CMD_2F_UNKNOWN,
  FT_INST_MIDI,
  FT_INST_IFFOCT,
  FT_INST_SYNTH,
  FT_INST_SYNTH_HYBRID,
  FT_INST_SYNTH_WF_GT_1,
  FT_INST_EXT,
  FT_INST_S16,
  FT_INST_STEREO,
  FT_INST_MD16,
  FT_INST_HOLD_DECAY,
  FT_INST_DEFAULT_PITCH,
  FT_INST_BIDI,
  FT_INST_DISABLE,
  FT_INST_LONG_REPEAT,
  FT_INST_LONG_REPEAT_DIFF,
  FT_INST_LONG_REPEAT_HIGH,
  FT_HYBRID_USES_IFFOCT,
  FT_HYBRID_USES_EXT,
  FT_HYBRID_USES_SYNTH,
  NUM_FEATURES
};

static const char * const FEATURE_DESC[NUM_FEATURES] =
{
  ">1Songs",
  "VarTracks",
  ">256Rows",
  "NoteHold",
  "Note1",
  "Oct4-7",
  "Oct8-A",
  "STrans",
  "ITrans",
  "8ChMode",
  "Tempo<=0A",
  "BRows!=4",
  "FilterOn",
  "ModSlide",
  "Tick0Slide",
  ">1CmdPages",
  "E:PortVol",
  "E:VibVol",
  "E:Tremolo",
  "E:HoldDecay",
  "E:900",
  "E:9<=20",
  "E:9>20",
  "E:FBrk",
  "E:FTwice",
  "E:FF1NoNote",
  "E:FDelay",
  "E:FThree",
  "E:FF3NoNote",
  "E:FF4",
  "E:FF5",
  "E:FFilter",
  "E:FPitch",
  "E:FStop",
  "E:FOff",
  "E:F<=0A",
  "E:F>0A",
  "E:FBPM<=2",
  "E:FBPM<=20",
  "E:FBPM",
  "E:FinePort",
  "E:PTVib",
  "E:Finetune",
  "E:Loop",
  "E:Loop>0F",
  "E:18Stop",
  "E:18Stop>0F",
  "E:Offset",
  "E:FineVol",
  "E:1DBrk",
  "E:PatDelay",
  "E:PatDelay>0F",
  "E:1FDelay",
  "E:1FRetrg",
  "E:1FBoth",
  "E:Reverse",
  "E:RelOffset",
  "E:LinearPort",
  "E:Pan",
  "E:EchoDepth",
  "E:StereoSep",
  "E:2F?",
  "I:MIDI",
  "I:IFFOct",
  "I:Synth",
  "I:Hybrid",
  "I:WF>1",
  "I:Ext",
  "I:S16",
  "I:Stereo",
  "I:Aura",
  "I:HoldDecay",
  "I:DefPitch",
  "I:Bidi",
  "I:Disable",
  "I:LongRep",
  "I:!=LongRep",
  "I:128kLongRep",
  "HybIFFOCT",
  "HybExt",
  "HybSyn(?!)",
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
  I_EXT     = 7,

  /* Flags */
  I_TYPEMASK = 0x07,
  I_S16      = 0x10,
  I_STEREO   = 0x20,
  I_MD16     = 0x18,
};

enum MMD3instrflags
{
  SSFLG_LOOP     = 0x01,
  SSFLG_EXTPSET  = 0x02,
  SSFLG_DISABLED = 0x04,
  SSFLG_PINGPONG = 0x08,
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
  if((t & ~I_S16 & ~I_STEREO & ~I_MD16) == 0)
  {
    if(t & I_S16)
    {
      if(t & I_STEREO)
        return "S16S";
      else
        return "S16";
    }
    else
    if(t & I_STEREO)
      return "SmpS";
  }
  return "???";
}

struct MED_cmd_info
{
  const char *cmd;
  const char *description;
  int params;
};

static const MED_cmd_info MED_bad_command =
{
  "???", "Unknown command", 0
};

static const MED_cmd_info *MED_volcommand_strs(uint8_t cmd)
{
  static const MED_cmd_info MED_volcommands[16] =
  {
    { "SPD",   "Volume sequence speed [#]",     1 },
    { "WAI",   "Wait [#]",                      1 },
    { "CHD",   "Change volume down [#]",        1 },
    { "CHU",   "Change volume up [#]",          1 },
    { "EN1",   "Envelope waveform [#]",         1 },
    { "EN2",   "Envelope waveform (loop) [#]",  1 },
    { "RES",   "Reset volume",                  0 },
    { nullptr, nullptr, 0 },
    { nullptr, nullptr, 0 },
    { nullptr, nullptr, 0 },
    { "JWS",   "Jump waveform sequence to [#]", 1 },
    { "HLT",   "Halt sequence",                 0 },
    { nullptr, nullptr, 0 },
    { nullptr, nullptr, 0 },
    { "JMP",   "Jump to [#]",                   1 },
    { "END",   "End sequence",                  -1 },
  };
  static const MED_cmd_info set_volume = { " # ", "Set volume", 0 };

  if(cmd >= 0xF0 && MED_volcommands[cmd - 0xF0].cmd)
    return &MED_volcommands[cmd - 0xF0];

  if(cmd >= 0x80)
    return &MED_bad_command;

  return &set_volume;
}

static const MED_cmd_info *MED_wfcommand_strs(uint8_t cmd)
{
  static const MED_cmd_info MED_wfcommands[16] =
  {
    { "SPD",   "Waveform sequence speed [#]",       1 },
    { "WAI",   "Wait [#]",                          1 },
    { "CHD",   "Change pitch down (period up) [#]", 1 },
    { "CHU",   "Change pitch up (period down) [#]", 1 },
    { "VBD",   "Vibrato depth [#]",                 1 },
    { "VBS",   "Vibrato speed [#]",                 1 },
    { "RES",   "Reset pitch",                       0 },
    { "VWF",   "Vibrato waveform [#]",              1 },
    { nullptr, nullptr, 0 },
    { nullptr, nullptr, 0 },
    { "JVS",   "Jump volume sequence to [#]",       1 },
    { "HLT",   "Halt sequence",                     0 },
    { "ARP",   "Arpeggio definition start [#...]",  0xFD },
    { "ARE",   "Arpeggio definition end",           0 },
    { "JMP",   "Jump to [#]",                       1 },
    { "END",   "End sequence",                      -1 },
  };
  static const MED_cmd_info set_waveform = { " # ", "Set waveform", 0 };

  if(cmd >= 0xF0 && MED_wfcommands[cmd - 0xF0].cmd)
    return &MED_wfcommands[cmd - 0xF0];

  if(cmd >= 0x80)
    return &MED_bad_command;

  return &set_waveform;
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
  E_CHANGE_VOL_CTRL  = 0x17,
  E_STOP_NOTE        = 0x18,
  E_SAMPLE_OFFSET    = 0x19,
  E_FINE_VOLUME_UP   = 0x1A,
  E_FINE_VOLUME_DOWN = 0x1B,
  E_CHANGE_MIDI_PRE  = 0x1C,
  E_PATTERN_BREAK    = 0x1D,
  E_PATTERN_DELAY    = 0x1E,
  E_DELAY_RETRIGGER  = 0x1F,
  E_REVERSE_REL_OFF  = 0x20,
  E_LINEAR_PORTA_UP  = 0x21,
  E_LINEAR_PORTA_DN  = 0x22,
  E_TRACK_PANNING    = 0x2E,
  E_ECHO_STEREO_SEP  = 0x2F,
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

struct MMD1block
{
  uint16_t num_tracks;       // uint8_t for MMD0
  uint16_t num_rows;         // uint8_t for MMD0 (stored as length - 1 for both)
  uint32_t blockinfo_offset; // MMD1 only.

  // These are from the BlockInfo struct.
  uint32_t  highlight_offset;
  uint32_t  block_name_offset;
  uint32_t  block_name_length;
  uint32_t  pagetable_offset;
  //uint32_t  reserved[6]; // just ignore these lol

  // These are from the BlockCmdPageTable struct.
  uint16_t  num_pages;
  //uint16_t  reserved;

  struct CommandPage
  {
    uint32_t offset = 0;
    bool loaded = false;
    std::vector<uint8_t> data;
  };

  std::vector<MMD0note>    events;
  std::vector<uint32_t>    highlight;
  std::vector<CommandPage> page;

  bool is_highlighted(unsigned row)
  {
    if(highlight.size() > row / 32)
    {
      uint32_t t = highlight[row / 32];
      uint32_t m = 1 << (row & 31);
      return (t & m) != 0;
    }
    return false;
  }
};

struct MMD0instr
{
  /*   0 */ uint32_t length;
  /*   4 */ int16_t  type;
};

struct MMD0synthWF
{
  /*   0 */ uint16_t length; // Divided by 2.
  /*   2 */ /* uint8_t data[]; */
};

/* Following fields are only present for synth instruments. */
struct MMD0synth
{
  /*   6 */ uint8_t  default_decay;        /* Used only when saving single instruments */
  /*   7 */ uint8_t  reserved[3];
  /*  10 */ uint16_t hy_repeat_start;      /* Used only when saving single instruments */
  /*  12 */ uint16_t hy_repeat_length;     /* Used only when saving single instruments */
  /*  14 */ uint16_t volume_table_length;
  /*  16 */ uint16_t waveform_table_length;
  /*  18 */ uint8_t  volume_table_speed;
  /*  19 */ uint8_t  waveform_table_speed;
  /*  20 */ uint16_t num_waveforms;
  /*  22 */ uint8_t  volume_table[128];
  /* 150 */ uint8_t  waveform_table[128];
  /* 278 */ uint32_t waveform_offsets[64]; /* struct SynthWF * */

  MMD0synthWF waveforms[64];
  /* Waveform 0 for hybrids is a sample. */
  MMD0instr hybrid_instrument;
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

struct MMD0
{
  MMD0head       header;
  MMD0song       song;
  MMD3exp        exp;
  MMD1block      patterns[MAX_BLOCKS];
  MMD0instr      instruments[MAX_INSTRUMENTS];
  MMD3instr_ext  instruments_ext[MAX_INSTRUMENTS];
  MMD3instr_info instruments_info[MAX_INSTRUMENTS];
  uint32_t       pattern_offsets[MAX_BLOCKS];
  uint32_t       instrument_offsets[MAX_INSTRUMENTS];
  uint32_t       num_tracks;
  bool           use_long_repeat;
  bool           uses[NUM_FEATURES];

  std::vector<char>          songname;
  std::unique_ptr<MMD0synth> synth_data[MAX_INSTRUMENTS];
};

static modutil::error read_mmd(FILE *fp, int mmd_version)
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
    return modutil::READ_ERROR;

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
    return modutil::READ_ERROR;

  /**
   * Song.
   */
  if(fseek(fp, h.song_offset, SEEK_SET))
    return modutil::SEEK_ERROR;

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
  /* FIXME this is completely wrong for MMD2/3 */
  s.num_orders      = fget_u16be(fp);

  if(!fread(s.orders, 256, 1, fp))
    return modutil::READ_ERROR;
  /* end FIXME */

  s.default_tempo   = fget_u16be(fp);
  s.transpose       = fgetc(fp);
  s.flags           = fgetc(fp);
  s.flags2          = fgetc(fp);
  s.tempo2          = fgetc(fp);

  if(s.transpose != 0)
    m.uses[FT_TRANSPOSE_SONG] = true;

  /* FIXME MMD2/3 handles track volume separately. */
  if(!fread(s.track_volume, 16, 1, fp))
    return modutil::READ_ERROR;

  s.song_volume     = fgetc(fp);
  s.num_instruments = fgetc(fp);

  if(feof(fp))
    return modutil::READ_ERROR;

  /**
   * Block array.
   */
  if(s.num_blocks > MAX_BLOCKS)
    return modutil::MED_TOO_MANY_BLOCKS;

  if(fseek(fp, h.block_array_offset, SEEK_SET))
    return modutil::SEEK_ERROR;

  for(size_t i = 0; i < s.num_blocks; i++)
    m.pattern_offsets[i] = fget_u32be(fp);

  /**
   * "Blocks" (aka patterns).
   */
  bool has_full_slides = false;
  for(size_t i = 0; i < s.num_blocks; i++)
  {
    if(!m.pattern_offsets[i])
      continue;

    if(fseek(fp, m.pattern_offsets[i], SEEK_SET))
      return modutil::SEEK_ERROR;

    MMD1block &b = m.patterns[i];

    if(mmd_version >= 1)
    {
      /* MMD1 through MMD3 */
      b.num_tracks       = fget_u16be(fp);
      b.num_rows         = fget_u16be(fp) + 1;
      b.blockinfo_offset = fget_u32be(fp);
      /* FIXME load blockinfo */
    }
    else
    {
      /* MMD0 */
      b.num_tracks = fgetc(fp);
      b.num_rows   = fgetc(fp) + 1;
    }

    if(m.num_tracks < b.num_tracks)
      m.num_tracks = b.num_tracks;

    if(b.num_rows > 256)
      m.uses[FT_OVER_256_ROWS] = true;

    if(b.num_tracks > 256 || b.num_rows > 9999)
    {
      format::warning("skipping nonsense block %zu\n", i);
      continue;
    }

    b.events.resize(b.num_tracks * b.num_rows, {});
    MMD0note *pat = b.events.data();

    MMD0note *current = pat;
    if(mmd_version != MMDC_VERSION)
    {
      for(size_t j = 0; j < b.num_rows; j++)
      {
        for(size_t k = 0; k < b.num_tracks; k++, current++)
        {
          int a = fgetc(fp);
          int b = fgetc(fp);
          int c = fgetc(fp);

          if(mmd_version >= 1)
          {
            /* MMD1 through MMD3 */
            int d = fgetc(fp);
            current->mmd1(a, b, c, d);
          }
          else
          {
            /* MMD0 */
            current->mmd0(a, b, c);
          }
        }
      }
    }
    else
    {
      size_t tmp_size = (size_t)b.num_rows * b.num_tracks * 3;
      std::vector<uint8_t> tmp(tmp_size);
      uint8_t *pos = tmp.data();
      uint8_t *end = pos + tmp_size;

      while(pos < end)
      {
        int pack = fgetc(fp);
        if(pack < 0)
          break;

        if(pack & 0x80)
        {
          /* Zero bytes. */
          pos += 256 - pack;
          continue;
        }

        /* No packing. */
        pack++;
        if(pack > end - pos)
          pack = end - pos;

        if(fread(pos, 1, pack, fp) < (size_t)pack)
          break;

        pos += pack;
      }
      pos = tmp.data();
      for(size_t j = 0; j < b.num_rows; j++)
      {
        for(size_t k = 0; k < b.num_tracks; k++, current++)
        {
          current->mmd0(pos[0], pos[1], pos[2]);
          pos += 3;
        }
      }
    }

    /* BlockInfo (MMD1+) */
    if(b.blockinfo_offset && fseek(fp, b.blockinfo_offset, SEEK_SET) == 0)
    {
      b.highlight_offset  = fget_u32be(fp);
      b.block_name_offset = fget_u32be(fp);
      b.block_name_length = fget_u32be(fp);
      b.pagetable_offset  = fget_u32be(fp);
      /* Several reserved words here... */

      if(Config.dump_pattern_rows && b.highlight_offset && fseek(fp, b.highlight_offset, SEEK_SET) == 0)
      {
        uint32_t highlight_len = (b.num_rows + 31)/32;
        b.highlight.resize(highlight_len);

        for(size_t j = 0; j < highlight_len; j++)
          b.highlight[j] = fget_u32be(fp);
      }

      if(b.pagetable_offset && fseek(fp, b.pagetable_offset, SEEK_SET) == 0)
      {
        b.num_pages  = fget_u16be(fp);
        /*reserved =*/ fget_u16be(fp);

        if(b.num_pages > 0)
          m.uses[FT_COMMAND_PAGES] = true;

        try
        {
          b.page.resize(b.num_pages);
        }
        catch(std::exception &e)
        {
          format::warning("failed to alloc pages for block %zu, ignoring: %zu", i, (size_t)b.num_pages);
          b.num_pages = 0;
        }

        for(unsigned j = 0; j < b.num_pages; j++)
          b.page[j].offset = fget_u32be(fp);

        for(unsigned j = 0; j < b.num_pages; j++)
        {
          if(fseek(fp, b.page[j].offset, SEEK_SET))
            continue;

          size_t len = b.num_tracks * b.num_rows * 2;

          b.page[j].data.resize(len);
          if(fread(b.page[j].data.data(), len, 1, fp))
            b.page[j].loaded = true;
        }
      }
    }

    /* Feature detection (common to all formats). */
    current = pat;
    bool is_bpm_mode = (s.flags2 & F2_BPM);
    for(size_t j = 0; j < b.num_rows; j++)
    {
      for(size_t k = 0; k < b.num_tracks; k++, current++)
      {
        /**
         * C-1=1, C#1=2... + 7 octaves.
         * Some songs actually rely on these high octaves playing very
         * low tones (see "childplay.med" by Blockhead).
         */
        if(current->note >= (1 + 12 * 7))
          m.uses[FT_OCTAVE_8] = true;
        else
        if(current->note >= (1 + 12 * 3))
          m.uses[FT_OCTAVE_4] = true;

        /* Hold symbols are stored as note 0 + instrument. */
        if(current->note == 0 && current->instrument > 0)
          m.uses[FT_NOTE_HOLD] = true;

        /* MED Soundstudio v2.1 emits note values of 1 to indicate that
         * the default note should be substituted. A large number of MMD0s
         * through MMD2s use this as a normal note, so only check MMD3. */
        if(current->note == 1 && mmd_version == 3)
          m.uses[FT_NOTE_1] = true;

        /* FIXME command pages */
        switch(current->effect)
        {
          case E_PORTAMENTO_UP:
          case E_PORTAMENTO_DOWN:
          case E_TONE_PORTAMENTO:
          case E_VOLUME_SLIDE_MOD:
          case E_VOLUME_SLIDE:
            if(current->param)
              has_full_slides = true;
            break;

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
              case 0xF4:
                m.uses[FT_CMD_DELAY_ONE_THIRD] = true;
                break;
              case 0xF5:
                m.uses[FT_CMD_DELAY_TWO_THIRDS] = true;
                break;
              case 0xF8: // Filter off
              case 0xF9: // Filter on
                m.uses[FT_CMD_FILTER] = true;
                break;
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
          {
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

          case E_REVERSE_REL_OFF:
            if(!current->param)
              m.uses[FT_CMD_20_REVERSE] = true;
            else
              m.uses[FT_CMD_20_RELATIVE_OFFSET] = true;
            break;
          case E_LINEAR_PORTA_UP:
          case E_LINEAR_PORTA_DN:
            m.uses[FT_CMD_LINEAR_PORTAMENTO] = true;
            break;
          case E_TRACK_PANNING:
            m.uses[FT_CMD_TRACK_PANNING] = true;
            break;

          case E_ECHO_STEREO_SEP:
          {
            if(current->param >= 0xe1 && current->param <= 0xe6)
              m.uses[FT_CMD_2F_ECHO_DEPTH] = true;
            else
            if((current->param >= 0xd0 && current->param <= 0xd4) ||
             (current->param >= 0xdc && current->param <= 0xdf))
              m.uses[FT_CMD_2F_STEREO_SEPARATION] = true;
            else
              m.uses[FT_CMD_2F_UNKNOWN] = true;
            break;
          }
        }
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
    return modutil::MED_TOO_MANY_INSTR;

  if(fseek(fp, h.sample_array_offset, SEEK_SET))
    return modutil::SEEK_ERROR;

  for(size_t i = 0; i < s.num_instruments; i++)
    m.instrument_offsets[i] = fget_u32be(fp);

  if(feof(fp))
    return modutil::READ_ERROR;

  /**
   * Instruments.
   */
  trace("instruments");
  for(size_t i = 0; i < s.num_instruments; i++)
  {
    trace("inst %zu offset is %zu", i+1, (size_t)m.instrument_offsets[i]);
    if(!m.instrument_offsets[i])
      continue;

    if(fseek(fp, m.instrument_offsets[i], SEEK_SET))
    {
      format::warning("skipping instrument %zu with invalid offset %zu", i+1, (size_t)m.instrument_offsets[i]);
      continue;
    }

    MMD0instr &inst = m.instruments[i];
    inst.length = fget_u32be(fp);
    inst.type   = fget_s16be(fp);
    trace("inst %zu length %zu type %d", i+1, (size_t)inst.length, inst.type);
    if(feof(fp))
    {
      format::warning("skipping instrument %zu past file end", i+1);
      continue;
    }

    if(inst.type == I_HYBRID || inst.type == I_SYNTH)
    {
      std::unique_ptr<MMD0synth> &syn = m.synth_data[i];
      std::unique_ptr<MMD0synth> tmp(new MMD0synth{});
      syn = std::move(tmp);

      MMD0instr &h_inst = syn->hybrid_instrument;

      trace("synth %zu", i+1);

      syn->default_decay         = fgetc(fp);
      syn->reserved[0]           = fgetc(fp);
      syn->reserved[1]           = fgetc(fp);
      syn->reserved[2]           = fgetc(fp);
      syn->hy_repeat_start       = fget_u16be(fp);
      syn->hy_repeat_length      = fget_u16be(fp);
      syn->volume_table_length   = fget_u16be(fp);
      syn->waveform_table_length = fget_u16be(fp);
      syn->volume_table_speed    = fgetc(fp);
      syn->waveform_table_speed  = fgetc(fp);
      syn->num_waveforms         = fget_u16be(fp);

      trace("synth %zu tables (vol: %d wf: %d)", i+1, syn->volume_table_length, syn->waveform_table_length);

      if(!fread(syn->volume_table, 128, 1, fp) ||
       !fread(syn->waveform_table, 128, 1, fp))
        return modutil::READ_ERROR;

      trace("synth %zu offsets (%d waveforms)", i+1, syn->num_waveforms);

      for(unsigned j = 0; j < syn->num_waveforms && j < MAX_WAVEFORMS; j++)
        syn->waveform_offsets[j] = fget_u32be(fp);

      for(unsigned j = 0; j < syn->num_waveforms && j < MAX_WAVEFORMS; j++)
      {
        trace("synth %zu waveform %u", i+1, j);
        if(fseek(fp, m.instrument_offsets[i] + syn->waveform_offsets[j], SEEK_SET))
        {
          format::warning("seek error, skipping synth %zu waveform %u", i+1, j);
          continue;
        }

        if(inst.type == I_HYBRID && j == 0)
        {
          /* Get the size and type of the sample. */
          h_inst.length = fget_u32be(fp);
          h_inst.type   = fget_s16be(fp);
          trace("hybrid %zu waveform 0 length %zu type %d", i+1, (size_t)h_inst.length, h_inst.type);
        }
        else
        {
          syn->waveforms[j].length = fget_u16be(fp);
          trace("synth %zu waveform %u length %u", i+1, j, syn->waveforms[j].length << 1);
        }
      }

      trace("synth %zu done", i+1);

      if(syn->num_waveforms > 1)
        m.uses[FT_INST_SYNTH_WF_GT_1] = true;

      if(inst.type == I_HYBRID)
      {
        m.uses[FT_INST_SYNTH_HYBRID] = true;

        if(h_inst.type < 0)
          m.uses[FT_HYBRID_USES_SYNTH] = true; /* Shouldn't happen? */
        else
        if((h_inst.type & I_TYPEMASK) == I_EXT)
          m.uses[FT_HYBRID_USES_EXT] = true;
        else
        if((h_inst.type & I_TYPEMASK) > 0)
          m.uses[FT_HYBRID_USES_IFFOCT] = true;

        if(h_inst.type > 0)
        {
          if((h_inst.type & I_MD16) == I_MD16)
            m.uses[FT_INST_MD16] = true;
          else
          if(h_inst.type & I_S16)
            m.uses[FT_INST_S16] = true;

          if(h_inst.type & I_STEREO)
            m.uses[FT_INST_STEREO] = true;
        }
      }
      else
        m.uses[FT_INST_SYNTH] = true;
    }
    else
    {
      if((inst.type & I_TYPEMASK) == I_EXT)
      {
        m.uses[FT_INST_EXT] = true;
      }
      else

      if((inst.type & I_TYPEMASK) > 0)
        m.uses[FT_INST_IFFOCT] = true;

      if((inst.type & I_MD16) == I_MD16)
        m.uses[FT_INST_MD16] = true;
      else
      if(inst.type & I_S16)
        m.uses[FT_INST_S16] = true;

      if(inst.type & I_STEREO)
        m.uses[FT_INST_STEREO] = true;
    }
  }

  /**
   * Expansion data.
   */
  trace("expdata");
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
      return modutil::READ_ERROR;

    if(x.songname_offset && x.songname_length && x.songname_length < 256)
    {
      trace("songname %08zx length %zu", (size_t)x.songname_offset, (size_t)x.songname_length);
      if(!fseek(fp, x.songname_offset, SEEK_SET))
      {
        m.songname.resize(x.songname_length + 1, '\0');
        if(fread(m.songname.data(), x.songname_length, 1, fp))
        {
          strip_module_name(m.songname.data(), x.songname_length);
        }
        else
        {
          format::warning("failed to load songname");
          m.songname[0] = '\0';
        }
      }
      else
        format::warning("failed to seek to songname");
    }

    if(x.sample_ext_entries > MAX_INSTRUMENTS)
      return modutil::MED_TOO_MANY_INSTR;

    if(x.sample_ext_entries && fseek(fp, x.sample_ext_offset, SEEK_SET))
      return modutil::SEEK_ERROR;

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
        m.use_long_repeat = true;
        skip -= 8;
      }

      if(skip && fseek(fp, skip, SEEK_CUR))
        return modutil::SEEK_ERROR;

      if(sx.hold)
        m.uses[FT_INST_HOLD_DECAY] = true;
      if(sx.default_pitch)
        m.uses[FT_INST_DEFAULT_PITCH] = true;
      if(sx.instrument_flags & SSFLG_DISABLED)
        m.uses[FT_INST_DISABLE] = true;
      if(sx.instrument_flags & SSFLG_PINGPONG)
        m.uses[FT_INST_BIDI] = true;
      if(x.sample_ext_size >= 18)
      {
        m.uses[FT_INST_LONG_REPEAT] = true;
        if(sx.long_repeat_start != (uint32_t)(s.samples[i].repeat_start << 1) ||
         sx.long_repeat_length != (uint32_t)(s.samples[i].repeat_length << 1))
          m.uses[FT_INST_LONG_REPEAT_DIFF] = true;

        if(sx.long_repeat_start >= (1<<17) || sx.long_repeat_length >= (1<<17))
          m.uses[FT_INST_LONG_REPEAT_HIGH] = true;
      }
    }

    if(x.instr_info_entries > MAX_INSTRUMENTS)
      return modutil::MED_TOO_MANY_INSTR;

    if(x.instr_info_entries && fseek(fp, x.instr_info_offset, SEEK_SET))
      return modutil::SEEK_ERROR;

    for(size_t i = 0; i < x.instr_info_entries; i++)
    {
      MMD3instr_info &sxi = m.instruments_info[i];
      int skip = x.instr_info_size;

      if(x.instr_info_size >= 40)
      {
        if(!fread(sxi.name, 40, 1, fp))
          return modutil::READ_ERROR;

        sxi.name[40] = '\0';
        skip -= 40;
      }
      if(skip && fseek(fp, skip, SEEK_CUR))
        return modutil::SEEK_ERROR;
    }
  }

  if(has_full_slides)
  {
    if(s.flags & F_MOD_SLIDES)
      m.uses[FT_MOD_SLIDES] = true;
    else
      m.uses[FT_TICK_0_SLIDES] = true;
  }

  if(s.flags & F_FILTER_ON)
    m.uses[FT_FILTER_ON] = true;

  if(s.flags & F_8_CHANNEL)
    m.uses[FT_8_CHANNEL_MODE] = true;

  if(h.num_extra_songs && x.nextmod_offset)
    m.uses[FT_MULTIPLE_SONGS] = true;

  if(m.songname.size())
    format::line("Name",   "%s", m.songname.data());
  format::line("Type",     "%4.4s", h.magic);
  format::line("Size",     "%u", h.file_length);
  format::line("Instr.",   "%u", s.num_instruments);
  format::line("Tracks",   "%u", m.num_tracks);
  format::line("Blocks",   "%u", s.num_blocks);
  format::line("Sequence", "%u", s.num_orders);

  if(s.flags2 & F2_BPM)
  {
    uint8_t beat_rows = (s.flags2 & F2_BPM_MASK) + 1;

    format::line("BPM",      "%u", s.default_tempo);
    format::line("BeatRows", "%u", beat_rows);
    format::line("Speed",    "%u", s.tempo2);

    if(beat_rows != 4)
      m.uses[FT_BEAT_ROWS_NOT_4] = true;
  }
  else
  {
    format::line("Tempo", "%u", s.default_tempo);
    format::line("Speed", "%u", s.tempo2);

    if(s.default_tempo >= 0x01 && s.default_tempo <= 0x0A)
      m.uses[FT_INIT_TEMPO_COMPAT] = true;
  }

  format::uses(m.uses, FEATURE_DESC);

  if(Config.dump_samples)
  {
    namespace table = format::table;

    static const char *labels[] =
    {
      "Name", "Type", "Hyb.", "Length", "LoopStart", "LoopLen", "MIDI", "", "Vol", "Tr.", "Hold/", "Decay", "Fine", "DefP", "Flg"
    };
    static const char *labels_long_repeat[] =
    {
      "Name", "Type", "Hyb.", "Start", "Long Start", "Length", "Long Length"
    };
    static const char *labels_synths[] =
    {
      "Name", "Type", "Hyb.", "#WFs", "VolLen", "VolSpd", "WFLen", "WFSpd"
    };

    table::table<
      table::string<40>,
      table::string<4>,
      table::string<4>,
      table::spacer,
      table::number<10>,
      table::number<10>,
      table::number<10>,
      table::spacer,
      table::number<4>,
      table::number<5>,
      table::spacer,
      table::number<4>,
      table::number<4>,
      table::number<4>,
      table::number<5>,
      table::number<4>,
      table::number<4>,
      table::number<3>>s_table;

    table::table<
      table::string<40>,
      table::string<4>,
      table::string<4>,
      table::spacer,
      table::number<7>,
      table::number<11>,
      table::spacer,
      table::number<7>,
      table::number<11>>lr_table;

    table::table<
      table::string<40>,
      table::string<4>,
      table::string<4>,
      table::spacer,
      table::number<4>,
      table::spacer,
      table::number<6>,
      table::number<6>,
      table::spacer,
      table::number<6>,
      table::number<6>>synth_table;

    format::line();
    s_table.header("Instr.", labels);
    for(unsigned int i = 0; i < s.num_instruments; i++)
    {
      MMD0sample     &sm = s.samples[i];
      MMD0instr      &si = m.instruments[i];
      MMD3instr_ext  &sx = m.instruments_ext[i];
      MMD3instr_info &sxi = m.instruments_info[i];
      auto           &ss = m.synth_data[i];

      unsigned int length         = si.length;
      unsigned int repeat_start   = m.use_long_repeat ? sx.long_repeat_start : sm.repeat_start * 2;
      unsigned int repeat_length  = m.use_long_repeat ? sx.long_repeat_length : sm.repeat_length * 2;
      unsigned int midi_preset    = sx.long_midi_preset ? sx.long_midi_preset : sm.midi_preset;
      unsigned int midi_channel   = sm.midi_channel;
      unsigned int default_volume = sm.default_volume;
      unsigned int default_pitch  = sx.default_pitch;
      unsigned int instr_flags    = sx.instrument_flags;
      int transpose = sm.transpose;

      unsigned int hold  = sx.hold;
      unsigned int decay = sx.decay;
      int finetune = sx.finetune;

      const char *hyb = (ss && si.type == I_HYBRID) ? MED_insttype_str(ss->hybrid_instrument.type): "";

      s_table.row(i + 1, sxi.name, MED_insttype_str(si.type), hyb, {},
       length, repeat_start, repeat_length, {},
       midi_channel, midi_preset, {},
       default_volume, transpose, hold, decay, finetune, default_pitch, instr_flags);
    }

    if(m.uses[FT_INST_LONG_REPEAT_DIFF])
    {
      format::line();
      lr_table.header("Instr.", labels_long_repeat);
      for(unsigned int i = 0; i < s.num_instruments; i++)
      {
        MMD0sample     &sm = s.samples[i];
        MMD0instr      &si = m.instruments[i];
        MMD3instr_ext  &sx = m.instruments_ext[i];
        MMD3instr_info &sxi = m.instruments_info[i];
        auto           &ss = m.synth_data[i];

        const char *hyb = (ss && si.type == I_HYBRID) ? MED_insttype_str(ss->hybrid_instrument.type): "";

        if(sx.long_repeat_start == (uint32_t)(sm.repeat_start << 1) &&
         sx.long_repeat_length == (uint32_t)(sm.repeat_length << 1))
          continue;

        lr_table.row(i + 1, sxi.name, MED_insttype_str(si.type), hyb, {},
         sm.repeat_start << 1, sx.long_repeat_start, {},
         sm.repeat_length << 1, sx.long_repeat_length);
      }
    }

    if(m.uses[FT_INST_SYNTH] || m.uses[FT_INST_SYNTH_HYBRID])
    {
      format::line();
      synth_table.header("Instr.", labels_synths);
      for(unsigned int i = 0; i < s.num_instruments; i++)
      {
        MMD0instr      &si = m.instruments[i];
        MMD3instr_info &sxi = m.instruments_info[i];
        auto           &ss = m.synth_data[i];

        const char *hyb = (ss && si.type == I_HYBRID) ? MED_insttype_str(ss->hybrid_instrument.type): "";

        if(si.type >= 0 || !ss)
          continue;

        synth_table.row(i + 1, sxi.name, MED_insttype_str(si.type), hyb, {},
         ss->num_waveforms, {},
         ss->volume_table_length, ss->volume_table_speed, {},
         ss->waveform_table_length, ss->waveform_table_speed);
      }
    }
  }

  if(Config.dump_samples_extra)
  {
    namespace table = format::table;

    static const char *labels_program[] =
    {
      "#", "Command", "Description"
    };
    static const char *labels_waveform[] =
    {
      "Offset", "Abs.Offset", "Length"
    };

    table::table<
      table::number<2,table::RIGHT|table::HEX|table::ZEROS>,
      table::string<8>,
      table::string<40>> program_table;

    table::table<
      table::number<10>,
      table::number<10>,
      table::number<10>> waveform_table;

    for(unsigned int i = 0; i < s.num_instruments; i++)
    {
      MMD0instr      &si = m.instruments[i];
      MMD3instr_info &sxi = m.instruments_info[i];
      auto           &ss = m.synth_data[i];

      const MED_cmd_info *cmd = nullptr;
      unsigned int j;
      int params;

      if(si.type >= 0)
        continue;

      format::endline();
      format::line("Synth", "%02x : %s", i + 1, sxi.name);

      format::line();
      program_table.header("Volume", labels_program);
      for(j = 0, params = 0; j < ss->volume_table_length && params >= 0; j++)
      {
        uint8_t val = ss->volume_table[j];
        if(params == 0 || (cmd->params >= 0x80 && cmd->params == val))
        {
          cmd = MED_volcommand_strs(val);
          params = cmd->params;
          program_table.row(j, val, cmd->cmd, cmd->description);
        }
        else
          program_table.row(j, val, "", ""), params--;
      }

      params = 0;
      format::line();
      program_table.header("WF    ", labels_program);
      for(j = 0, params = 0; j < ss->waveform_table_length && params >= 0; j++)
      {
        uint8_t val = ss->waveform_table[j];
        if(params == 0 || (cmd->params >= 0x80 && cmd->params == val))
        {
          cmd = MED_wfcommand_strs(val);
          params = cmd->params;
          program_table.row(j, val, cmd->cmd, cmd->description);
        }
        else
          program_table.row(j, val, "", ""), params--;
      }

      if(!ss->num_waveforms)
        continue;

      format::line();
      waveform_table.header("WFs   ", labels_waveform);
      for(j = 0; j < ss->num_waveforms && j < MAX_WAVEFORMS; j++)
      {
        uint32_t offset = m.instrument_offsets[i] + ss->waveform_offsets[j];
        uint32_t length = (si.type == I_HYBRID && j == 0) ? ss->hybrid_instrument.length : ss->waveforms[j].length << 1;
        waveform_table.row(j, ss->waveform_offsets[j], offset, length);
      }
    }
  }

  if(Config.dump_patterns)
  {
    format::line();
    format::orders("Sequence", s.orders, s.num_orders);

    if(!Config.dump_pattern_rows)
      format::line();

    for(unsigned int i = 0; i < s.num_blocks; i++)
    {
      MMD1block &b = m.patterns[i];

      // TODO: MMD1+ supports up to 256(?) effect channels via blockinfo.
      using EVENT = format::event<format::note<>, format::sample<>, format::effectWide>;
      format::pattern<EVENT> pattern(i, b.num_tracks, b.num_rows);
      pattern.labels("Blk.", "Block");

      if(!Config.dump_pattern_rows || !b.events.size())
      {
        pattern.summary();
        continue;
      }

      MMD0note *current = b.events.data();
      for(unsigned int row = 0; row < b.num_rows; row++)
      {
        for(unsigned int track = 0; track < b.num_tracks; track++, current++)
        {
          format::note<>     a{ current->note };
          format::sample<>   b{ current->instrument };
          format::effectWide c{ current->effect, current->param };

          pattern.insert(EVENT(a, b, c));
        }
      }
      pattern.print();
    }
  }

  return modutil::SUCCESS;
}


static modutil::error read_med2(FILE *fp)
{
  format::line("Type", "MED2");
  num_med2++;
  return modutil::NOT_IMPLEMENTED;
}

static modutil::error read_med3(FILE *fp)
{
  format::line("Type", "MED3");
  num_med3++;
  return modutil::NOT_IMPLEMENTED;
}

static modutil::error read_med4(FILE *fp)
{
  format::line("Type", "MED4");
  num_med4++;
  return modutil::NOT_IMPLEMENTED;
}

static modutil::error read_mmd0(FILE *fp)
{
  num_mmd0++;
  return read_mmd(fp, 0);
}

static modutil::error read_mmd1(FILE *fp)
{
  num_mmd1++;
  return read_mmd(fp, 1);
}

static modutil::error read_mmd2(FILE *fp)
{
  num_mmd2++;
  return read_mmd(fp, 2);
}

static modutil::error read_mmd3(FILE *fp)
{
  num_mmd3++;
  return read_mmd(fp, 3);
}

static modutil::error read_mmdc(FILE *fp)
{
  num_mmdc++;
  return read_mmd(fp, MMDC_VERSION);
}

struct MED_handler
{
  const char *magic;
  modutil::error (*read_fn)(FILE *fp);
};

static const MED_handler HANDLERS[] =
{
  { MAGIC_MED2, read_med2 },
  { MAGIC_MED3, read_med3 },
  { MAGIC_MED4, read_med4 },
  { MAGIC_MMD0, read_mmd0 },
  { MAGIC_MMD1, read_mmd1 },
  { MAGIC_MMD2, read_mmd2 },
  { MAGIC_MMD3, read_mmd3 },
  { MAGIC_MMDC, read_mmdc },
};

class MED_loader : modutil::loader
{
public:
  MED_loader(): modutil::loader("MED", "med", "MED/OctaMED") {}

  virtual modutil::error load(modutil::data state) const override
  {
    FILE *fp = state.reader.unwrap(); /* FIXME: */

    char magic[4];
    if(!fread(magic, 4, 1, fp))
      return modutil::FORMAT_ERROR;

    rewind(fp);

    for(const MED_handler &handler : HANDLERS)
    {
      if(!memcmp(handler.magic, magic, 4))
      {
        num_med++;
        return handler.read_fn(fp);
      }
    }
    return modutil::FORMAT_ERROR;
  }

  virtual void report() const override
  {
    if(!num_med)
      return;

    format::report("Total MEDs", num_med);

    if(num_med2)
      format::reportline("Total MED2s", "%d", num_med2);
    if(num_med3)
      format::reportline("Total MED3s", "%d", num_med3);
    if(num_med4)
      format::reportline("Total MED4s", "%d", num_med4);
    if(num_mmd0)
      format::reportline("Total MMD0s", "%d", num_mmd0);
    if(num_mmd1)
      format::reportline("Total MMD1s", "%d", num_mmd1);
    if(num_mmd2)
      format::reportline("Total MMD2s", "%d", num_mmd2);
    if(num_mmd3)
      format::reportline("Total MMD3s", "%d", num_mmd3);
    if(num_mmdc)
      format::reportline("Total MMDCs", "%d", num_mmdc);
  }
};

static const MED_loader loader;
