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

#include "modutil.hpp"

static int num_669;
static int num_composer;
static int num_unis;


class _669_loader : public modutil::loader
{
public:
  _669_loader(): modutil::loader("669", "669", "Composer 669") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    char magic[2];
    if(!fread(magic, 2, 1, fp))
      return modutil::FORMAT_ERROR;

    if(!memcmp(magic, "if", 2))
    {
      format::line("Type", "Composer 669");
      num_composer++;
    }
    else

    if(!memcmp(magic, "JN", 2))
    {
      format::line("Type", "UNIS 669");
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

    format::report("Total 669s", num_669);
    if(num_composer)
      format::reportline("Composer 669s", "%d", num_composer);
    if(num_unis)
      format::reportline("UNIS 669",      "%d", num_unis);
  }
};

static const _669_loader loader;
