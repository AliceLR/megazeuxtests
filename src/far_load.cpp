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
#include "modutil.hpp"

static int total_far = 0;


static const char magic[] = "FAR\xFE";

struct FAR_header
{
  char magic[4];
  char name[40];
  char eof[3];
  uint8_t header_length[2];
  uint8_t version;
  uint8_t track_enabled[16];
  uint8_t current_oct;
  uint8_t current_voice;
  uint8_t current_row;
  uint8_t current_pat;
  uint8_t current_ord;
  uint8_t current_sam;
  uint8_t current_vol;
  uint8_t current_display;
  uint8_t current_editing;
  uint8_t current_tempo;
  uint8_t track_panning[16];
  uint8_t mark_top;
  uint8_t mark_bottom;
  uint8_t grid_size;
  uint8_t edit_mode;
  uint16_t text_length;
};

struct FAR_orders
{
  uint8_t orders[256];
  uint8_t num_patterns;
  uint8_t num_orders;
  uint8_t loop_to_position;
};

struct FAR_pattern_metadata
{
  uint16_t expected_rows;
  uint8_t break_location;
};

struct FAR_data
{
  struct FAR_header h;
  struct FAR_orders o;
  uint16_t pattern_length[256];
  struct FAR_pattern_metadata p[256];
  char *text = nullptr;

  ~FAR_data()
  {
    delete[] text;
  }
};


class FAR_loader : modutil::loader
{
public:
  FAR_loader(): modutil::loader("FAR : Farandole Composer") {}

  virtual modutil::error load(FILE *fp) const override
  {
    FAR_data d{};
    FAR_header &h = d.h;
    size_t len;
    int num_patterns;
    int i;

    if(!fread(&h, sizeof(struct FAR_header), 1, fp))
      return modutil::READ_ERROR;

    if(memcmp(h.magic, magic, 4))
      return modutil::FORMAT_ERROR;

    total_far++;

    O_("Version : %x\n", h.version);
    if(h.version != 0x10)
    {
      O_("Error   : unknown FAR version %02x\n", h.version);
      return modutil::BAD_VERSION;
    }

    fix_u16le(h.text_length);

    len = h.text_length;
    //O_("FAR text length: %u\n", (unsigned int)len);
    if(len)
    {
      d.text = new char[len + 1];
      if(!fread(d.text, len, 1, fp))
        return modutil::READ_ERROR;
      d.text[len] = '\0';
    }

    if(!fread(&(d.o), sizeof(struct FAR_orders), 1, fp))
      return modutil::READ_ERROR;

    if(!fread(d.pattern_length, sizeof(d.pattern_length), 1, fp))
      return modutil::READ_ERROR;

    // This value isn't always correct...
    num_patterns = d.o.num_patterns;
    for(i = 0; i < 256; i++)
    {
      fix_u16le(d.pattern_length[i]);
      if(d.pattern_length[i])
      {
        size_t rows = (d.pattern_length[i] - 2) >> 6;
        if(i < num_patterns && rows > 256)
          O_("warning: pattern %02x expects %u rows >256\n", i, (unsigned int)rows);

        d.p[i].expected_rows = rows;

        if(num_patterns < i + 1)
          num_patterns = i + 1;
      }
    }
    O_("Patterns: %d (claims %d)\n", num_patterns, d.o.num_patterns);

    if(Config.dump_patterns)
    {
      O_("        :\n");
      O_("Patterns:\n");
      O_("------- :\n");
      for(i = 0; i < num_patterns; i++)
      {
        int pattern_len = d.pattern_length[i];
        int expected_rows = d.p[i].expected_rows;
        int break_location;
        if(!pattern_len)
        {
          O_("    %02x  : length=%u, ignoring.\n", i, pattern_len);
          continue;
        }

        break_location = fgetc(fp);
        if(feof(fp))
        {
          O_("Error   : read error for pattern %02x!\n", i);
          return modutil::READ_ERROR;
        }

        d.p[i].break_location = break_location;

        O_("    %02x  : length=%u, expected_rows=%u, break byte=%u, difference=%d\n",
          i, pattern_len, expected_rows, break_location, expected_rows - break_location
        );

        if(Config.dump_pattern_rows)
        {
          fgetc(fp); // Pattern tempo byte. "DO NOT SUPPORT IT!!".

          for(int j = 0; j < expected_rows; j++)
          {
            fprintf(stderr, ":");
            for(int k = 0; k < 16; k++)
            {
              int note = fgetc(fp);
              int inst = fgetc(fp);
              int vol  = fgetc(fp);
              int fx   = fgetc(fp);
              if(feof(fp))
              {
                O_("pattern read error for pattern %02x!\n", i);
                return modutil::READ_ERROR;
              }

#define P_PRINT(x) if(x) fprintf(stderr, " %02x", x); else fprintf(stderr, "   ");
              P_PRINT(note);
              P_PRINT(inst);
              P_PRINT(vol);
              P_PRINT(fx);
              fprintf(stderr, ":");
            }
            fprintf(stderr, "\n");
          }
          fprintf(stderr, "\n");
          fflush(stderr);
        }
        else
          fseek(fp, d.pattern_length[i] - 1, SEEK_CUR);
      }
    }
    // ignore samples
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!total_far)
      return;

    fprintf(stderr, "\n");
    O_("Total FARs          : %d\n", total_far);
    O_("------------------- :\n");
  }
};

static const FAR_loader loader;
