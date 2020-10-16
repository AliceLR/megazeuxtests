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
  "669util determines whether a 669 is from Composer 669 or UNIS 669.\n"
  "That's all it does.\n\n"
  "Usage:\n"
  "  669util [filename.ext...]\n\n"
  "A list of filenames can be provided via stdin:\n"
  "  ls -1 | 669util -\n\n";

static int num_669;
static int num_unis;
static int num_unknown;

bool read_669(FILE *fp)
{
  char magic[2];
  if(!fread(magic, 2, 1, fp))
    return false;

  if(!memcmp(magic, "if", 2))
  {
    O_("File is a Composer 669.\n\n");
    num_669++;
  }
  else

  if(!memcmp(magic, "JN", 2))
  {
    O_("File is a UNIS 669.\n\n");
    num_unis++;
  }
  else
  {
    O_("File is not a 669.\n\n");
    num_unknown++;
  }
  return true;
}

void check_669(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("Checking '%s'...\n", filename);

    if(!read_669(fp))
      O_("Error reading file.\n\n");

    fclose(fp);
  }
  else
    O_("Failed to open '%s'.\n\n", filename);
}


int main(int argc, char *argv[])
{
  if(!argv || argc < 2)
  {
    fprintf(stderr, "%s", USAGE);
    return 0;
  }

  bool read_stdin = false;
  for(int i = 1; i < argc; i++)
  {
    if(!strcmp(argv[i], "-"))
    {
      if(!read_stdin)
      {
        char buffer[1024];
        while(fgets_safe(buffer, stdin))
          check_669(buffer);
        read_stdin = true;
      }
      continue;
    }
    check_669(argv[i]);
  }

  if(num_669)
    O_("Total Composer 669s : %d\n", num_669);
  if(num_unis)
    O_("Total UNIS 669s     : %d\n", num_unis);
  if(num_unknown)
    O_("Total unknown       : %d\n", num_unknown);
  return 0;
}
