/**
 * Copyright (C) 2021 Lachesis <petrifiedrowan@gmail.com>
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

/**
 * Utility for dumping and aggregating DSMI AMF module information.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Config.hpp"
#include "common.hpp"

static const char USAGE[] =
  "A utility to dump DSMI AMF metadata and patterns.\n"
  "Usage:\n"
  "  amfutil [options] [filenames...]\n\n";

enum AMF_err
{
  AMF_SUCCESS,
  AMF_ALLOC_ERROR,
  AMF_READ_ERROR,
  AMF_SEEK_ERROR,
  AMF_BAD_SIGNATURE,
  AMF_BAD_VERSION,
  AMF_BAD_CHANNELS,
  AMF_BAD_TRACKS,
  AMF_BAD_TRACK_IDX,
};

static const char *AMF_strerror(int err)
{
  switch(err)
  {
    case AMF_SUCCESS:       return "no error";
    case AMF_READ_ERROR:    return "read error";
    case AMF_SEEK_ERROR:    return "seek error";
    case AMF_BAD_SIGNATURE: return "AMF signature mismatch";
    case AMF_BAD_VERSION:   return "AMF version invalid";
    case AMF_BAD_CHANNELS:  return "too many channels";
    case AMF_BAD_TRACKS:    return "too many tracks";
    case AMF_BAD_TRACK_IDX: return "invalid track index in order";
  }
  return "unknown error";
}

enum AMF_features
{
  FT_BLANK_TRACK,
  FT_OUT_OF_BOUNDS_TRACK,
  FT_REPEAT_EVENT_AT_ROW_0,
  FT_2_EFFECTS,
  FT_3_EFFECTS,
  FT_4_EFFECTS,
  FT_TOO_MANY_EFFECTS,
  FT_FX_UNKNOWN,
  FT_FX_SPEED,
  FT_FX_VOLSLIDE,
  FT_FX_VOLUME,
  FT_FX_PORTAMENTO,
  FT_FX_PORTAMENTO_ABS,
  FT_FX_TONEPORTA,
  FT_FX_TREMOR,
  FT_FX_ARPEGGIO,
  FT_FX_VIBRATO,
  FT_FX_VOLSLIDE_TONEPORTA,
  FT_FX_VOLSLIDE_VIBRATO,
  FT_FX_BREAK,
  FT_FX_JUMP,
  FT_FX_SYNC,
  FT_FX_RETRIGGER,
  FT_FX_OFFSET,
  FT_FX_VOLSLIDE_FINE,
  FT_FX_PORTAMENTO_FINE,
  FT_FX_NOTE_DELAY,
  FT_FX_NOTE_CUT,
  FT_FX_BPM,
  FT_FX_PORTAMENTO_EXTRA_FINE,
  FT_FX_PAN,
  FT_FX_PAN_SURROUND,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "Track0",
  "Track>Max",
  "Rep0",
  "2fx",
  "3fx",
  "4fx",
  ">4fx",
  "FXUnknown",
  "FXSpeed",
  "FXVolslide",
  "FXVolume",
  "FXPorta",
  "FXPortAbs",
  "FXToneporta",
  "FXTremor",
  "FXArpeg",
  "FXVibr",
  "FXVolPorta",
  "FXVolVib",
  "FXBreak",
  "FXJump",
  "FXSync",
  "FXRetrig",
  "FXOffset",
  "FXVolFine",
  "FXPortaFine",
  "FXNDelay",
  "FXNCut",
  "FXBPM",
  "FXPortaExF",
  "FXPan",
  "FXSurround",
};

static const char *AMF_effect_strings[23] =
{
  " A",
  "vs",
  " v",
  "po",
  "pa",
  " G",
  " I",
  " J",
  " H",
  " L",
  " K",
  " C",
  " B",
  "sy",
  " Q",
  " O",
  "vf",
  "pf",
  "SD",
  "SC",
  " T",
  "pe",
  " X",
};

enum AMF_sampletypes
{
  SAMPLE_NONE = 0,
  SAMPLE_PCM  = 1,
};

static constexpr size_t AMF_MAX_ORDERS   = 256;
static constexpr size_t AMF_MAX_CHANNELS = 32;
static constexpr size_t AMF_MAX_TRACKS   = AMF_MAX_ORDERS * AMF_MAX_CHANNELS;

static const char magic[] = "AMF";

struct AMF_order
{
  uint16_t tracks[AMF_MAX_CHANNELS];
  uint16_t real_tracks[AMF_MAX_CHANNELS];
  uint16_t num_rows;
};

struct AMF_sample
{
  uint8_t  type;
  char     name[32];
  char     filename[13];
  uint32_t index;
  uint32_t length;     /* is a word <= 0x09 */
  uint16_t c4speed;
  uint8_t  volume;
  uint32_t loop_start; /* is a word <= 0x09 */
  uint32_t loop_end;   /* is a word <= 0x09 */
};

struct AMF_event
{
  static constexpr uint8_t NOTEVOL = (1<<4);
  static constexpr uint8_t SAMPLE  = (1<<5);
  static constexpr uint8_t FX      = (0x0F);
  static constexpr uint8_t MAX_FX  = 4;
  static constexpr uint8_t INC_FX  = 1;

  uint8_t note;
  uint8_t volume;
  uint8_t sample;
  uint8_t flags;
  struct
  {
    uint8_t effect;
    uint8_t param;
  } fx[MAX_FX];
};

struct AMF_track
{
  uint32_t   num_rows;
  uint32_t   calculated_size;
  uint32_t   offset_in_file;
  uint16_t   num_events;
  uint8_t    unknown;
  uint8_t    event_flag_xor;
  uint8_t   *raw_data = nullptr;
  AMF_event *track_data = nullptr;

  ~AMF_track()
  {
    delete[] raw_data;
    delete[] track_data;
  }

  void init()
  {
    delete[] raw_data;
    delete[] track_data;

    if(calculated_size)
      raw_data = new uint8_t[calculated_size];

    if(num_rows)
      track_data = new AMF_event[num_rows]{};
  }
};

struct AMF_module
{
  /*  0 */ char     magic[3];
  /*  3 */ uint8_t  version;
  /*  4 */ char     name[32];
  /* 36 */ uint8_t  num_samples;
  /* 37 */ uint8_t  num_orders;
  /* 38 */ uint16_t num_tracks;

  /* AMF 0x09 and up. */
  /* 40 */ uint8_t  num_channels;

  /* AMF 0x09 and 0x0A only. */
  /* 41 */ uint8_t  channel_remap[16];

  /* AMF 0x0B and up. */
  /* Note: 0x0B, 0x0C only have 16 values here? */
  /* 41 */ uint8_t  channel_panning[32];
  /* 73 */ uint8_t  initial_tempo;
  /* 74 */ uint8_t  initial_speed;

  uint16_t          *track_table = nullptr;
  size_t            real_num_tracks;

  struct AMF_order  *orders = nullptr;
  struct AMF_sample *samples = nullptr;
  struct AMF_track  *tracks = nullptr;

  uint8_t highest_fx_count = 0;
  bool uses[NUM_FEATURES];

  ~AMF_module()
  {
    delete[] track_table;
    delete[] orders;
    delete[] samples;
    delete[] tracks;
  }
};

static int AMF_read(FILE *fp)
{
  AMF_module m{};

  if(!fread(m.magic, sizeof(m.magic), 1, fp))
    return AMF_READ_ERROR;

  if(memcmp(m.magic, "AMF", 3))
    return AMF_BAD_SIGNATURE;

  m.version = fgetc(fp);

  O_("Version   : %3.3s %02x\n", m.magic, m.version);

  if(m.version != 0x01 && (m.version < 0x08 || m.version > 0x0E))
    return AMF_BAD_VERSION;

  if(!fread(m.name, 32, 1, fp))
    return AMF_READ_ERROR;

  m.name[31] = '\0';

  m.num_samples = fgetc(fp);
  m.num_orders  = fgetc(fp);
  m.num_tracks  = fget_u16le(fp);

  if(m.version >= 0x09)
    m.num_channels = fgetc(fp);
  else
    m.num_channels = 4;

  if(m.num_channels > AMF_MAX_CHANNELS)
    return AMF_BAD_CHANNELS;

  if(m.num_tracks > AMF_MAX_TRACKS)
    return AMF_BAD_TRACKS;

  // Channel panning and/or remap.
  if(m.version >= 0x0B)
  {
    size_t num_panning = (m.version >= 0x0C) ? 32 : 16;
    for(size_t i = 0; i < num_panning; i++)
      m.channel_panning[i] = fgetc(fp);
  }
  else

  if(m.version >= 0x09)
  {
    for(size_t i = 0; i < 16; i++)
      m.channel_remap[i] = fgetc(fp);
  }

  // Initial tempo and speed.
  if(m.version >= 0x0D)
  {
    m.initial_tempo = fgetc(fp);
    m.initial_speed = fgetc(fp);
  }
  else
  {
    m.initial_tempo = 125;
    m.initial_speed = 6;
  }

  if(feof(fp))
    return AMF_READ_ERROR;

  // Order table.
  m.orders = new AMF_order[m.num_orders]{};
  for(size_t i = 0; i < m.num_orders; i++)
  {
    AMF_order &order = m.orders[i];

    if(m.version >= 0x0E)
      order.num_rows = fget_u16le(fp);
    else
      order.num_rows = 64;

    for(size_t j = 0; j < m.num_channels; j++)
    {
      order.tracks[j] = fget_u16le(fp);
      if(order.tracks[j] > m.num_tracks)
        return AMF_BAD_TRACK_IDX;
    }
  }

  // Sample table.
  m.samples = new AMF_sample[m.num_samples]{};
  for(size_t i = 0; i < m.num_samples; i++)
  {
    AMF_sample &sample = m.samples[i];

    sample.type = fgetc(fp);
    if(!fread(sample.name, 32, 1, fp))
      return AMF_READ_ERROR;

    if(!fread(sample.filename, 13, 1, fp))
      return AMF_READ_ERROR;

    sample.index = fget_u32le(fp);

    if(m.version >= 0x0A)
    {
      sample.length     = fget_u32le(fp);
      sample.c4speed    = fget_u16le(fp);
      sample.volume     = fgetc(fp);
      sample.loop_start = fget_u32le(fp);
      sample.loop_end   = fget_u32le(fp);
    }
    else
    {
      sample.length     = fget_u16le(fp);
      sample.c4speed    = fget_u16le(fp);
      sample.volume     = fgetc(fp);
      sample.loop_start = fget_u16le(fp);
      sample.loop_end   = fget_u16le(fp);
    }

    sample.name[31] = '\0';
    sample.filename[12] = '\0';
  }

  // Track table.
  m.track_table = new uint16_t[m.num_tracks + 1];
  m.track_table[0] = 0;
  m.real_num_tracks = 0;
  for(size_t i = 1; i <= m.num_tracks; i++)
  {
    m.track_table[i] = fget_u16le(fp);
    if(m.track_table[i] > m.real_num_tracks)
      m.real_num_tracks = m.track_table[i];
  }

  // Populate orders with the real track indices.
  for(size_t i = 0; i < m.num_orders; i++)
  {
    AMF_order &order = m.orders[i];
    for(size_t j = 0; j < m.num_channels; j++)
    {
      uint16_t track = order.tracks[j];

      if(track > m.num_tracks)
      {
        m.uses[FT_OUT_OF_BOUNDS_TRACK] = true;
        track = 0;
      }
      else

      if(m.track_table[track] == 0)
        m.uses[FT_BLANK_TRACK] = true;

      order.real_tracks[j] = m.track_table[track];
    }
  }

  // Track data.
  m.tracks = new AMF_track[m.real_num_tracks + 1]{};
  m.tracks[0].num_rows = 64;
  m.tracks[0].init();

  for(size_t i = 1; i <= m.real_num_tracks; i++)
  {
    AMF_track &track = m.tracks[i];

    track.offset_in_file  = ftell(fp);
    track.num_events      = fget_u16le(fp); // NOTE: according to Saga Musix, ver 1 may add +1. Need test file
    track.unknown         = fgetc(fp);
    track.calculated_size = track.num_events * 3;
    track.num_rows        = 64; // FIXME lol
    track.event_flag_xor  = 0;

    track.init();

    if(!fread(track.raw_data, track.calculated_size, 1, fp))
      return AMF_READ_ERROR;

    // Translate packed data to expanded form.
    for(size_t j = 0; j < track.calculated_size; j += 3)
    {
      uint8_t row   = track.raw_data[j + 0];
      uint8_t cmd   = track.raw_data[j + 1];
      uint8_t param = track.raw_data[j + 2];

      if(row >= track.num_rows)
        break;

      AMF_event &ev = track.track_data[row];
      if(cmd < 0x7f)
      {
        // Note.
        ev.flags  |= AMF_event::NOTEVOL;
        ev.note   = cmd;
        ev.volume = param;
      }
      else

      if(cmd == 0x7f)
      {
        // Copy previous event.
        if(row == 0) // Shouldn't happen?
        {
          m.uses[FT_REPEAT_EVENT_AT_ROW_0] = true;
          continue;
        }

        ev = track.track_data[row - 1];
      }
      else

      if(cmd == 0x80)
      {
        // Sample change.
        ev.flags  |= AMF_event::SAMPLE;
        ev.sample = param;
      }
      else
      {
        // Effect.
        uint8_t fx = (ev.flags & AMF_event::FX);

        if(fx + AMF_event::INC_FX > m.highest_fx_count)
          m.highest_fx_count = fx + AMF_event::INC_FX;

        if(fx == AMF_event::MAX_FX) // Shouldn't happen?
          continue;

        switch(cmd)
        {
          case 0x81: // Speed (Axx)
            m.uses[FT_FX_SPEED] = true;
            break;
          case 0x82: // Volslide (signed: >0 Dx0, <0 D0x)
            m.uses[FT_FX_VOLSLIDE] = true;
            break;
          case 0x83: // Channel volume (PT Cxx)
            m.uses[FT_FX_VOLUME] = true;
            break;
          case 0x84: // Portamento (signed: >0 Exx, <0 Fxx)
            m.uses[FT_FX_PORTAMENTO] = true;
            break;
          case 0x85: // "Porta Abs" (unknown)
            m.uses[FT_FX_PORTAMENTO_ABS] = true;
            break;
          case 0x86: // Tone Portamento (Gxx)
            m.uses[FT_FX_TONEPORTA] = true;
            break;
          case 0x87: // Tremor (Ixx)
            m.uses[FT_FX_TREMOR] = true;
            break;
          case 0x88: // Arpeggio (doc claims PT 0xx)
            m.uses[FT_FX_ARPEGGIO] = true;
            break;
          case 0x89: // Vibrato (doc claims PT 4xx)
            m.uses[FT_FX_VIBRATO] = true;
            break;
          case 0x8A: // Volslide + Toneporta (signed: >0 Lx0, <0 L0x)
            m.uses[FT_FX_VOLSLIDE_TONEPORTA] = true;
            break;
          case 0x8B: // Volslide + Vibrato (signed: >0 Kx0, <0 K0x)
            m.uses[FT_FX_VOLSLIDE_VIBRATO] = true;
            break;
          case 0x8C: // Break
            m.uses[FT_FX_BREAK] = true;
            break;
          case 0x8D: // Jump
            m.uses[FT_FX_JUMP] = true;
            break;
          case 0x8E: // "Sync" (unknown)
            m.uses[FT_FX_SYNC] = true;
            break;
          case 0x8F: // Retrigger (Q0x)
            m.uses[FT_FX_RETRIGGER] = true;
            break;
          case 0x90: // Offset (PT 9xx)
            m.uses[FT_FX_OFFSET] = true;
            break;
          case 0x91: // Volslide (fine) (signed: >0 DxF, <0 DFx)
            m.uses[FT_FX_VOLSLIDE_FINE] = true;
            break;
          case 0x92: // Portamento (fine) (signed: >0 EFx, <0 FFx)
            m.uses[FT_FX_PORTAMENTO_FINE] = true;
            break;
          case 0x93: // Note delay (PT EDx)
            m.uses[FT_FX_NOTE_DELAY] = true;
            break;
          case 0x94: // Note cut (PT ECx)
            m.uses[FT_FX_NOTE_CUT] = true;
            break;
          case 0x95: // BPM (Txx)
            m.uses[FT_FX_BPM] = true;
            break;
          case 0x96: // Portamento (extra fine) (signed: >0 EEx, <0 FEx)
            m.uses[FT_FX_PORTAMENTO_EXTRA_FINE] = true;
            break;
          case 0x97: // Pan + Surround (Xxx, range 0x00 to 0x80 with 0xA4=surround?)
            if(param == 0xA4)
              m.uses[FT_FX_PAN_SURROUND] = true;
            else
              m.uses[FT_FX_PAN] = true;
            break;
          default:
            m.uses[FT_FX_UNKNOWN] = true;
        }

        ev.flags += AMF_event::INC_FX;
        ev.fx[fx].effect = cmd;
        ev.fx[fx].param  = param;
      }

      track.event_flag_xor |= ev.flags;
    }
  }

  switch(m.highest_fx_count)
  {
    case 0:
    case 1:
      break;
    case 2:
      m.uses[FT_2_EFFECTS] = true;
      break;
    case 3:
      m.uses[FT_3_EFFECTS] = true;
      break;
    case 4:
      m.uses[FT_4_EFFECTS] = true;
      break;
    default:
      m.uses[FT_TOO_MANY_EFFECTS] = true;
  }

  // Print metadata.
  O_("Title     : %s\n", m.name);
  O_("Samples   : %u\n", m.num_samples);
  O_("Orders    : %u\n", m.num_orders);
  O_("Tracks    : %u (%zu)\n", m.num_tracks, m.real_num_tracks);
  O_("Channels  : %u\n", m.num_channels);

  O_("Uses      :");
  for(int i = 0; i < NUM_FEATURES; i++)
    if(m.uses[i])
      fprintf(stderr, " %s", FEATURE_STR[i]);
  fprintf(stderr, "\n");

  if(Config.dump_samples)
  {
    if(m.num_samples)
    {
      O_("          :\n");
      O_("Samples   : D.Vol  C4 Rate : Length      Loop Start  Loop End   \n");
      O_("-------   : -----  ------- : ----------  ----------  ---------- \n");
      for(unsigned int i = 0; i < m.num_samples; i++)
      {
        AMF_sample &sample = m.samples[i];
        O_("Sample %02x : %-5u  %-7u : %-10u  %-10u  %-10u\n",
          i + 1, sample.volume, sample.c4speed,
          sample.length, sample.loop_start, sample.loop_end
        );
      }
    }
  }

  if(Config.dump_patterns)
  {
    O_("          :\n");
    O_("Orders    :\n");
    O_("------    :\n");

    for(unsigned int i = 0; i < m.num_orders; i++)
    {
      AMF_order &order = m.orders[i];
      O_("Order %02x  : %-3u rows : ", i, order.num_rows);

      for(unsigned int j = 0; j < m.num_channels; j++)
        fprintf(stderr, " %04x ", order.real_tracks[j]);

      fprintf(stderr, "\n");
    }

    O_("          :\n");
    O_("Tracks    : Offset      Events  ??? :\n");
    O_("------    : ----------  ------  --- :\n");

    for(unsigned int i = 0; i < m.real_num_tracks; i++)
    {
      AMF_track &track = m.tracks[i];
      if(!track.raw_data)
        continue;

      O_("Track %02x  : %-10u  %-6u  %-3u :\n",
       i, track.offset_in_file, track.num_events, track.unknown);
    }

    if(Config.dump_pattern_rows)
    {
      // Raw track data.
      if(m.real_num_tracks > 1)
        O_("          :\n");

      for(unsigned int i = 0; i < m.real_num_tracks; i++)
      {
        AMF_track &track = m.tracks[i];
        if(!track.raw_data)
          continue;

        O_("Track %02x  : ", i);
        for(unsigned int j = 0; j < track.calculated_size; j += 3)
        {
          if(j && !(j % 24))
          {
            // Insert break.
            fprintf(stderr, "\n");
            O_("          : ");
          }

          fprintf(stderr, "%02x %02x %02x  ",
           track.raw_data[j + 0], track.raw_data[j + 1], track.raw_data[j + 2]);
        }
        fprintf(stderr, "\n");
      }

      // Assemble human-readable patterns.
      static const char *BLANK = "                             ";
      static const char *LINE  = "-----------------------------";

      O_("          :\n");
      O_("Effect Key: ");
      for(unsigned int i = 0; i < arraysize(AMF_effect_strings); i++)
        fprintf(stderr, "%s%s=%02x", (i > 0)?",":"", AMF_effect_strings[i], i + 0x81);
      fprintf(stderr, "\n");

      for(unsigned int i = 0; i < m.num_orders; i++)
      {
        AMF_order &order = m.orders[i];

        AMF_track *ord_tracks[AMF_MAX_CHANNELS];
        int ord_widths[AMF_MAX_CHANNELS];

        O_("          :\n");
        O_("Order %02x  :", i);

        // Get track pointers, compute widths, and print header.
        for(size_t j = 0; j < m.num_channels; j++)
        {
          uint16_t track_id = order.real_tracks[j];
          ord_tracks[j] = &m.tracks[track_id];
          uint8_t flags = ord_tracks[j]->event_flag_xor;
          int width = 0;

          if(flags & AMF_event::SAMPLE)
          {
            // Width needs to be at least 6, add note column.
            flags |= AMF_event::NOTEVOL;
            width += 3;
          }
          if(flags & AMF_event::NOTEVOL)
            width += 6;
          if(flags & AMF_event::FX)
            width += 5 * (flags & AMF_event::FX);

          ord_widths[j] = width;
          if(width > 0)
          {
            if(width < 6)
              width = 6;
            fprintf(stderr, " T%04x%.*s :", track_id, width - 6, BLANK);
          }
        }
        fprintf(stderr, "\n");

        O_("--------  :");
        for(size_t j = 0; j < m.num_channels; j++)
        {
          int width = ord_widths[j];
          if(width > 0)
          {
            if(width < 6)
              width = 6;
            fprintf(stderr, " %.*s :", width - 1, LINE);
          }
        }
        fprintf(stderr, "\n");

        // Print pattern body.
        for(unsigned int row = 0; row < order.num_rows; row++)
        {
          O_("      %02x  :", row);
          for(unsigned int col = 0; col < m.num_channels; col++)
          {
            int width = ord_widths[col];
            if(width < 1)
              continue;

            AMF_track &track = *ord_tracks[col];
            if(col < track.num_rows)
            {
              AMF_event &ev = track.track_data[row];
              uint8_t flags = track.event_flag_xor;

              if(flags & AMF_event::NOTEVOL)
              {
                if(ev.flags & AMF_event::NOTEVOL)
                  fprintf(stderr, " %02x %02x", ev.note, ev.volume);
                else
                  fprintf(stderr, "      ");
              }

              if(flags & AMF_event::SAMPLE)
              {
                if(ev.flags & AMF_event::SAMPLE)
                  fprintf(stderr, " %02x", ev.sample);
                else
                  fprintf(stderr, "   ");
              }

              if(flags & AMF_event::FX)
              {
                uint8_t num_fx = (flags & AMF_event::FX);
                uint8_t ev_fx = (ev.flags & AMF_event::FX);
                for(unsigned int j = 0; j < num_fx; j++)
                {
                  char tmp[3];
                  if(j < ev_fx)
                  {
                    uint8_t effect = ev.fx[j].effect;
                    sprintf(tmp, "%02x", effect);
                    fprintf(stderr, " %s%02X",
                      (effect >= 0x81 && effect <= 0x97) ? AMF_effect_strings[effect - 0x81] : tmp,
                      ev.fx[j].param
                    );
                  }
                  else
                    fprintf(stderr, "     ");
                }
              }
            }
            else
              width = 0;

            if(width < 6)
              fprintf(stderr, "%.*s", 6 - width, BLANK);
            fprintf(stderr, " :");
          }
          fprintf(stderr, "\n");
        }
      }
    }
  }

  return AMF_SUCCESS;
}

static void check_AMF(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("File      : %s\n", filename);

    int err = AMF_read(fp);
    if(err)
      O_("Error     : %s\n\n", AMF_strerror(err));
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
    fprintf(stdout, "%s%s", USAGE, Config.COMMON_FLAGS);
    return 0;
  }

  if(!Config.init(&argc, argv))
    return -1;

  for(int i = 1; i < argc; i++)
  {
    if(!strcmp(argv[i], "-"))
    {
      if(!read_stdin)
      {
        char buffer[1024];
        while(fgets_safe(buffer, stdin))
          check_AMF(buffer);

        read_stdin = true;
      }
      continue;
    }
    check_AMF(argv[i]);
  }

  return 0;
}
