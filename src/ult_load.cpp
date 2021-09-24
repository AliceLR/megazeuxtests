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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Config.hpp"
#include "common.hpp"
#include "modutil.hpp"

static int total_ults = 0;


static constexpr char MAGIC[] = "MAS_UTrack_V00";

enum ULT_features
{
  FT_SAMPLE_16BIT,
  FT_SAMPLE_REVERSE,
  FT_SAMPLE_BIT7,
  NUM_FEATURES
};

static const char * const FEATURE_DESC[NUM_FEATURES] =
{
  "S:16",
  "S:Rev",
  "S:bit7",
};


enum ULT_versions
{
  ULT_V1_0 = 1,
  ULT_V1_4 = 2,
  ULT_V1_5 = 3,
  ULT_V1_6 = 4,
};

enum ULT_sample_flags
{
  S_16BIT   = (1<<2),
  S_LOOP    = (1<<3),
  S_REVERSE = (1<<4),
};

struct ULT_sample
{
  /*  0 */ char     name[33]; /* Stored as 32 bytes. */
  /* 32 */ char     filename[13]; /* Stored as 12 bytes. */
  /* 44 */ uint32_t loop_start;
  /* 48 */ uint32_t loop_end;
  /* 52 */ uint32_t size_start; /* Used for GUS memory management. */
  /* 56 */ uint32_t size_end; /* Same. */
  /* 60 */ uint8_t  default_volume;
  /* 61 */ uint8_t  bidi; /* flags */
  /* 62 */ int16_t  finetune;

  /* V1.6: this goes between bidi and finetune. */
  /* 62 */ uint16_t c2speed;

  /* Calculated with size_start/size_end. */
  uint32_t length;
};

struct ULT_event
{
  uint8_t note;
  uint8_t sample;
  uint8_t effect;
  uint8_t effect2;
  uint8_t param;
  uint8_t param2;

  ULT_event(uint8_t n=0, uint8_t s=0, uint8_t fx=0, uint8_t p2=0, uint8_t p1=0):
   note(n), sample(s), param(p1), param2(p2)
  {
    effect  = (fx & 0xf0) >> 4;
    effect2 = (fx & 0x0f);
  }
};

struct ULT_pattern
{
  ULT_event *events = nullptr;
  uint16_t channels;
  uint16_t rows;

  ULT_pattern(uint16_t c = 0, uint16_t r = 0): channels(c), rows(r)
  {
    if(c && r)
      events = new ULT_event[c * r]{};
  }
  ~ULT_pattern()
  {
    delete[] events;
  }
  ULT_pattern &operator=(ULT_pattern &&p)
  {
    events = p.events;
    channels = p.channels;
    rows = p.rows;
    p.events = nullptr;
    p.channels = 0;
    p.rows = 0;
    return *this;
  }
};

struct ULT_header
{
  /*  0   */ char    magic[15];
  /* 15   */ char    title[32];
  /* 47   */ uint8_t text_length; /* V1.4 ('V002'): The (value * 32) bytes following this are the song text. */
  /* 48+x */ uint8_t num_samples; /* NOT stored as 0 -> 1, unlike the channels/patterns... */

  /* After samples: */
  /*   0 */ uint8_t  orders[256];
  /* 256 */ uint16_t num_channels; /* Stored as uint8_t, 0 -> 1. */
  /* 257 */ uint16_t num_patterns; /* Stored as uint8_t, 0 -> 1. */

  /* V1.5 ('V003'): panning table. */
  /* 258 */ uint8_t panning[256];
};

struct ULT_data
{
  ULT_header  header;
  ULT_sample  *samples = nullptr;
  ULT_pattern *patterns = nullptr;
  char *text = nullptr;
  size_t text_length;

  char title[33];
  int version;
  uint16_t num_orders;
  bool uses[NUM_FEATURES];

  ~ULT_data()
  {
    delete[] samples;
    delete[] patterns;
    delete[] text;
  }
};


class ULT_loader : modutil::loader
{
public:
  ULT_loader(): modutil::loader("ULT : Ultra Tracker") {}

  virtual modutil::error load(FILE *fp) const override
  {
    ULT_data m{};
    ULT_header &h = m.header;
    int err;

    /**
     * Header (part 1).
     */
    if(!fread(h.magic, sizeof(h.magic), 1, fp))
      return modutil::READ_ERROR;

    if(memcmp(h.magic, MAGIC, sizeof(h.magic)-1))
      return modutil::FORMAT_ERROR;

    total_ults++;
    if(h.magic[14] < '1' || h.magic[14] > '4')
    {
      O_("Error   : unknown ULT version 0x%02x\n", h.magic[14]);
      return modutil::BAD_VERSION;
    }
    m.version = h.magic[14] - '0';

    if(!fread(h.title, sizeof(h.title), 1, fp))
      return modutil::READ_ERROR;

    memcpy(m.title, h.title, sizeof(h.title));
    m.title[sizeof(h.title)] = '\0';

    strip_module_name(m.title, sizeof(m.title));

    /**
     * Text.
     */
    h.text_length = err = fgetc(fp);
    if(err < 0)
      return modutil::READ_ERROR;

    if(m.version >= ULT_V1_4 && h.text_length)
    {
      m.text_length = h.text_length * 32;
      m.text = new char[m.text_length + 1];
      if(!fread(m.text, m.text_length, 1, fp))
        return modutil::READ_ERROR;
      m.text[m.text_length] = '\0';
    }

    /**
     * Instruments.
     */
    h.num_samples = err = fgetc(fp);
    if(err < 0)
      return modutil::READ_ERROR;

    m.samples = new ULT_sample[h.num_samples]{};
    for(size_t i = 0; i < h.num_samples; i++)
    {
      ULT_sample &ins = m.samples[i];

      if(!fread(ins.name, 32, 1, fp) ||
         !fread(ins.filename, 12, 1, fp))
        return modutil::READ_ERROR;

      ins.name[32] = '\0';
      ins.filename[12] = '\0';

      ins.loop_start     = fget_u32le(fp);
      ins.loop_end       = fget_u32le(fp);
      ins.size_start     = fget_u32le(fp);
      ins.size_end       = fget_u32le(fp);
      ins.default_volume = fgetc(fp);
      ins.bidi           = fgetc(fp);
      if(m.version >= ULT_V1_6)
        ins.c2speed      = fget_u16le(fp);
      ins.finetune     = fget_s16le(fp);

      ins.length = (ins.size_end > ins.size_start) ? ins.size_end - ins.size_start : 0;

      if(ins.bidi & S_16BIT)
        m.uses[FT_SAMPLE_16BIT] = true;
      if(ins.bidi & S_REVERSE)
        m.uses[FT_SAMPLE_REVERSE] = true;
      // Not sure what this is, found it in "sea of emotions.ult".
      if(ins.bidi & 128)
        m.uses[FT_SAMPLE_BIT7] = true;
    }

    /**
     * Header (part 2).
     */
    if(!fread(h.orders, 256, 1, fp))
      return modutil::READ_ERROR;

    size_t ord;
    for(ord = 0; ord < 256; ord++)
      if(h.orders[ord] == 0xff)
        break;
    m.num_orders = ord;

    h.num_channels = fgetc(fp) + 1;
    h.num_patterns = fgetc(fp) + 1;

    if(m.version >= ULT_V1_5)
    {
      if(!fread(h.panning, h.num_channels, 1, fp))
        return modutil::READ_ERROR;
    }

    /**
     * Patterns.
     */
    m.patterns = new ULT_pattern[h.num_patterns]{};

    for(size_t i = 0; i < h.num_patterns; i++)
    {
      ULT_pattern &p = m.patterns[i];
      p = ULT_pattern(h.num_channels, 64);

      for(size_t j = 0; j < h.num_channels; j++)
      {
        ULT_event *current = p.events + j;

        for(size_t k = 0; k < 64;)
        {
          uint8_t arr[7];
          if(!fread(arr, 5, 1, fp))
            return modutil::READ_ERROR;

          if(arr[0] == 0xfc)
          {
            // RLE.
            if(!fread(arr + 5, 2, 1, fp))
              return modutil::READ_ERROR;

            ULT_event tmp(arr[2], arr[3], arr[4], arr[5], arr[6]);
            int c = arr[1];
            do
            {
              *current = tmp;
              current += p.channels;
              c--;
              k++;
            }
            while(c > 0 && k < 64);
          }
          else
          {
            *current = ULT_event(arr[0], arr[1], arr[2], arr[3], arr[4]);
            current += p.channels;
            k++;
          }
        }
      }
    }

    /**
     * Print info.
     */
    O_("Title   : %s\n", m.title);
    O_("Type    : ULT V00%d\n", m.version);
    O_("Samples : %u\n", h.num_samples);
    O_("Channels: %u\n", h.num_channels);
    O_("Patterns: %u\n", h.num_patterns);
    O_("Orders  : %u\n", m.num_orders);

    O_("Uses    :");
    for(int i = 0; i < NUM_FEATURES; i++)
      if(m.uses[i])
        fprintf(stderr, " %s", FEATURE_DESC[i]);
    fprintf(stderr, "\n");

    if(Config.dump_samples)
    {
      O_("        :\n");
      O_("Samples : Name                             Filename     : Length     LoopStart  LoopEnd    : Vol Flg Speed Fine   :\n");
      O_("------- : -------------------------------- ------------ : ---------- ---------- ---------- : --- --- ----- ------ :\n");
      for(unsigned int i = 0; i < h.num_samples; i++)
      {
        ULT_sample &ins = m.samples[i];

        O_("    %02x  : %-32.32s %-12.12s : %10u %10u %10u : %3u %3u %5u %6d :\n",
          i + 1, ins.name, ins.filename,
          ins.length, ins.loop_start, ins.loop_end,
          ins.default_volume, ins.bidi, ins.c2speed, ins.finetune
        );
      }
    }

    if(Config.dump_patterns)
    {
      O_("        :\n");
      O_("Orders  :");
      for(size_t i = 0; i < m.num_orders; i++)
        fprintf(stderr, " %02x", h.orders[i]);
      fprintf(stderr, "\n");

      for(unsigned int i = 0; i < h.num_patterns; i++)
      {
        if(!Config.dump_pattern_rows)
          break;

        ULT_pattern &p = m.patterns[i];

        bool p_note[256]{};
        bool p_inst[256]{};
        bool p_eff[256]{};
        bool p_eff2[256]{};
        int p_sz[256]{};
        bool print_pattern = false;

        // Do a quick scan of the block to see how much info to print...
        ULT_event *current = p.events;
        for(unsigned int row = 0; row < p.rows; row++)
        {
          for(unsigned int track = 0; track < p.channels; track++, current++)
          {
            p_eff2[track] |= (current->effect2 != 0) || (current->param2 != 0);
            p_eff[track]  |= (current->effect != 0)  || (current->param != 0) || p_eff2[track];
            p_inst[track] |= current->sample != 0 || p_eff[track];
            p_note[track] |= current->note != 0 || p_inst[track];

            p_sz[track] = (p_note[track] * 3) + (p_inst[track] * 3) + (p_eff[track] * 4) + (p_eff2[track] * 4);
            print_pattern |= (p_sz[track] > 0);
          }
        }

        if(!print_pattern)
        {
          O_("Pat. %02x : pattern is blank. \n", i);
          continue;
        }

        fprintf(stderr, "\n");
        O_("Pat. %02x :\n", i);
        O_("\n");

        O_("");
        for(unsigned int track = 0; track < p.channels; track++)
          if(p_sz[track])
            fprintf(stderr, " %02x%*s:", track, p_sz[track] - 2, "");
        fprintf(stderr, "\n");

        O_("");
        for(unsigned int track = 0; track < p.channels; track++)
          if(p_sz[track])
            fprintf(stderr, "%.*s:", p_sz[track] + 1, "----------------");
        fprintf(stderr, "\n");

        current = p.events;
        for(unsigned int row = 0; row < p.rows; row++)
        {
          fprintf(stderr, ": ");

          for(unsigned int track = 0; track < p.channels; track++, current++)
          {
            if(!p_sz[track])
              continue;

#define P_PRINT(x) do{ if(x) fprintf(stderr, " %02x", x); else fprintf(stderr, "   "); }while(0)
#define P_PRINTX(x,y) do{ if(x || y) fprintf(stderr, " %X%02x", x, y); else fprintf(stderr, "    "); }while(0)

            if(p_note[track])
              P_PRINT(current->note);
            if(p_inst[track])
              P_PRINT(current->sample);
            if(p_eff[track])
              P_PRINTX(current->effect, current->param);
            if(p_eff2[track])
              P_PRINTX(current->effect2, current->param2);
            fprintf(stderr, " :");
          }
          fprintf(stderr, "\n");
        }
      }
    }

    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!total_ults)
      return;

    fprintf(stderr, "\n");
    O_("Total ULTs          : %d\n", total_ults);
    O_("------------------- :\n");
  }
};

static const ULT_loader loader;
