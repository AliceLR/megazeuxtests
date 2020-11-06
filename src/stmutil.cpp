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
  "Dump information about STM files.\n\n"
  "Usage:\n"
  "  stmutil [filename.ext...]\n\n";

static bool dump_samples;
static bool dump_patterns;
static bool dump_pattern_rows;

enum STM_error
{
  STM_SUCCESS,
  STM_READ_ERROR,
  STM_SEEK_ERROR,
  STM_NOT_AN_STM,
  STM_NOT_IMPLEMENTED,
};

static const char *STM_strerror(int err)
{
  switch(err)
  {
    case STM_SUCCESS:         return "no error";
    case STM_READ_ERROR:      return "read error";
    case STM_SEEK_ERROR:      return "seek error";
    case STM_NOT_AN_STM:      return "not an .STM";
    case STM_NOT_IMPLEMENTED: return "feature not implemented";
  }
  return "unknown error";
}

enum STM_features
{
  FT_TYPE_SONG,
  FT_TYPE_MODULE,
  NUM_FEATURES
};

static const char * const FEATURE_DESC[NUM_FEATURES] =
{
  "T:Song",
  "T:Module",
};

enum STM_types
{
  TYPE_SONG = 1,
  TYPE_MODULE = 2,
};

struct STM_header
{
  char name[20];
  char tracker[8];
  uint8_t eof;
  uint8_t type;
  uint8_t version_maj;
  uint8_t version_min;
};

struct STM_module
{
  STM_header header;
  char name[21];
  bool uses[NUM_FEATURES];
};

static int STM_read(FILE *fp)
{
  STM_module m{};
  STM_header &h = m.header;

  if(!fread(&h, sizeof(STM_header), 1, fp))
    return STM_READ_ERROR;

  memcpy(m.name, h.name, 20);
  m.name[20] = '\0';

  strip_module_name(m.name, 20);

  if(h.type == TYPE_SONG)
    m.uses[FT_TYPE_SONG] = true;
  if(h.type == TYPE_MODULE)
    m.uses[FT_TYPE_MODULE] = true;

  O_("Name      : %s\n",      m.name);
  O_("Tracker   : %8.8s\n",   h.tracker);
  O_("Version   : %u.%02u\n", h.version_maj, h.version_min);

  O_("Uses      :");
  for(int i = 0; i < NUM_FEATURES; i++)
    if(m.uses[i])
      fprintf(stderr, " %s", FEATURE_DESC[i]);
  fprintf(stderr, "\n");

/*
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
        i + 1, STM_insttype_str(si.type), length, repeat_start, repeat_length,
        midi_channel, midi_preset, default_volume, transpose, hold, decay, finetune, sxi.name
      );
    }
  }

  if(dump_patterns)
  {
    O_("          :\n");
    O_("Sequence  :");
    for(size_t i = 0; i < s.num_orders; i++)
      fprintf(stderr, " %02x", s.orders[i]);
    fprintf(stderr, "\n");

    for(unsigned int i = 0; i < s.num_blocks; i++)
    {
      MMD1block &b = m.patterns[i];
      MMD0note *data = m.pattern_data[i];

      if(dump_pattern_rows)
        fprintf(stderr, "\n");

      O_("Block %02x  : %u rows, %u tracks\n", i, b.num_rows, b.num_tracks);

      if(!dump_pattern_rows)
        continue;

      uint8_t p_note[256]{};
      uint8_t p_inst[256]{};
      uint8_t p_eff[256]{};
      int p_sz[256]{};
      bool print_pattern = false;

      // Do a quick scan of the block to see how much info to print...
      MMD0note *current = data;
      for(unsigned int row = 0; row < b.num_rows; row++)
      {
        for(unsigned int track = 0; track < b.num_tracks; track++, current++)
        {
          p_eff[track]  |= (current->effect != 0) || (current->param != 0);
          p_inst[track] |= current->instrument != 0 || p_eff[track];
          p_note[track] |= current->note != 0 || p_inst[track];

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
        fprintf(stderr, m.highlight(i, row) ? "X " : ": ");

        for(unsigned int track = 0; track < b.num_tracks; track++, current++)
        {
          if(!p_sz[track])
            continue;

#define P_PRINT(x) do{ if(x) fprintf(stderr, " %02x", x); else fprintf(stderr, "   "); }while(0)
#define P_PRINT2(x,y) do{ if(x || y) fprintf(stderr, " %02x %02x", x, y); else fprintf(stderr, "      "); }while(0)

          if(p_note[track])
            P_PRINT(current->note);
          if(p_inst[track])
            P_PRINT(current->instrument);
          if(p_eff[track])
            P_PRINT2(current->effect, current->param);
          fprintf(stderr, " :");
        }
        fprintf(stderr, "\n");
      }
    }
  }
*/
  return STM_SUCCESS;
}

static void STM_check(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    setvbuf(fp, NULL, _IOFBF, 2048);

    O_("File      : %s\n", filename);

    int err = STM_read(fp);
    if(err)
      O_("Error     : %s\n\n", STM_strerror(err));
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
              STM_check(buffer);

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
    STM_check(arg);
  }
  return 0;
}
