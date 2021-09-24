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
#include "modutil.hpp"

static int num_669;
static int num_composer;
static int num_unis;


class _669_loader : public modutil::loader
{
public:
  _669_loader(): modutil::loader("669 : Composer 669") {}

  virtual modutil::error load(FILE *fp) const override
  {
    char magic[2];
    if(!fread(magic, 2, 1, fp))
      return modutil::READ_ERROR;

    if(!memcmp(magic, "if", 2))
    {
      O_("Type    : Composer 669\n");
      num_composer++;
    }
    else

    if(!memcmp(magic, "JN", 2))
    {
      O_("Type    : UNIS 669\n");
      num_unis++;
    }
    else
      return modutil::FORMAT_ERROR;

    num_669++;
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!num_669)
      return;

    fprintf(stderr, "\n");
    O_("Total 669s          : %d\n", num_669);
    O_("------------------- :\n");

    if(num_composer)
      O_("Composer 669s       : %d\n", num_composer);
    if(num_unis)
      O_("UNIS 669s           : %d\n", num_unis);
  }
};

static const _669_loader loader;
