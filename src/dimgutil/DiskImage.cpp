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

#include <stdint.h>
#include <stdio.h>
#include <memory>
#include <vector>

#include "DiskImage.hpp"


static std::vector<DiskImageLoader *> &get_list()
{
  static std::vector<DiskImageLoader *> _list;
  return _list;
}

DiskImageLoader::DiskImageLoader()
{
  get_list().push_back(this);
}

DiskImage *DiskImageLoader::TryLoad(FILE *fp)
{
  for(DiskImageLoader *l : get_list())
  {
    rewind(fp);
    DiskImage *img = l->Load(fp);
    if(img)
      return img;
  }
  return nullptr;
}
