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

/**
 * Utility for checking .FAR pattern lengths vs. break byte values.
 * This really doesn't do much else right now.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.hpp"

enum FAR_err
{
  FAR_SUCCESS,
  FAR_ALLOC_ERROR,
  FAR_READ_ERROR,
  FAR_SEEK_ERROR,
  FAR_BAD_SIGNATURE,
  FAR_BAD_VERSION,
};

static const char *far_strerror(int err)
{
  switch(err)
  {
    case FAR_SUCCESS:       return "no error";
    case FAR_READ_ERROR:    return "read error";
    case FAR_SEEK_ERROR:    return "seek error";
    case FAR_BAD_SIGNATURE: return "FAR signature mismatch";
    case FAR_BAD_VERSION:   return "FAR version invalid";
  }
  return "unknown error";
}


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
  char *text;
};

int far_read(struct FAR_data *d, FILE *fp)
{
  size_t len;
  int num_patterns;
  int i;

  memset(d, 0, sizeof(struct FAR_data));

  if(!fread(&(d->h), sizeof(struct FAR_header), 1, fp))
    return FAR_READ_ERROR;

  if(memcmp(d->h.magic, magic, 4))
    return FAR_BAD_SIGNATURE;

  O_("FAR version %x\n", d->h.version);
  if(d->h.version != 0x10)
    return FAR_BAD_VERSION;

  fix_u16le(d->h.text_length);

  len = d->h.text_length;
  O_("FAR text length: %u\n", (unsigned int)len);
  if(len)
  {
    d->text = (char *)malloc(len + 1);
    if(!d->text)
      return FAR_ALLOC_ERROR;

    if(!fread(d->text, len, 1, fp))
      return FAR_READ_ERROR;
    d->text[len] = '\0';
  }

  if(!fread(&(d->o), sizeof(struct FAR_orders), 1, fp))
    return FAR_READ_ERROR;

  if(!fread(d->pattern_length, sizeof(d->pattern_length), 1, fp))
    return FAR_READ_ERROR;

  num_patterns = d->o.num_patterns;
  O_("alleged pattern count: %d\n", num_patterns); // this is a lie

  for(i = 0; i < 256; i++)
  {
    fix_u16le(d->pattern_length[i]);
    if(d->pattern_length[i])
    {
      size_t rows = (d->pattern_length[i] - 2) >> 6;
      if(i < num_patterns && rows > 256)
        O_("warning: pattern %d expects %u rows >256\n", i, (unsigned int)rows);

      d->p[i].expected_rows = rows;

      if(num_patterns < i + 1)
        num_patterns = i + 1;
    }
  }
  O_("real pattern count: %d\n", num_patterns);

  for(i = 0; i < num_patterns; i++)
  {
    int pattern_len = d->pattern_length[i];
    int expected_rows = d->p[i].expected_rows;
    int break_location;
    if(!pattern_len)
    {
      O_("pattern %d: length=%u, ignoring.\n", i, pattern_len);
      continue;
    }

    break_location = fgetc(fp);
    if(feof(fp))
    {
      O_("pattern read error for pattern %d!\n", i);
      return FAR_READ_ERROR;
    }

    O_("pattern %d: length=%u, expected_rows=%u, break byte=%u, difference=%d\n",
      i, pattern_len, expected_rows, break_location, expected_rows - break_location
    );

    d->p[i].break_location = break_location;
    fseek(fp, d->pattern_length[i] - 1, SEEK_CUR);
  }
  // ignore samples
  return 0;
}

void far_free(struct FAR_data *d)
{
  free(d->text);
  d->text = NULL;
}

void check_far(const char *filename)
{
  struct FAR_data d;
  int ret;

  size_t len = strlen(filename);
  if(len < 4 || strcasecmp(filename + len - 4, ".far"))
    return;

  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("checking '%s'.\n", filename);

    ret = far_read(&d, fp);
    if(ret)
      O_("failed to read .far file: %s.\n\n", far_strerror(ret));
    else
      O_("read .far file successfully.\n\n");

    fclose(fp);
    far_free(&d);
  }
  else
    O_("failed to open '%s'\n.", filename);
}


int main(int argc, char *argv[])
{
  bool read_stdin = false;

  if(!argv || argc < 2)
  {
    fprintf(stdout, "Usage: %s filenames...\n", argv ? argv[0] : "farutil");
    return 0;
  }

  for(int i = 1; i < argc; i++)
  {
    if(!strcmp(argv[i], "-"))
    {
      if(!read_stdin)
      {
        char buffer[1024];
        while(fgets_safe(buffer, stdin))
          check_far(buffer);

        read_stdin = true;
      }
      continue;
    }
    check_far(argv[i]);
  }

  return 0;
}
