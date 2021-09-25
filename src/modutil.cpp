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

#include <stdlib.h>
#include <algorithm>
#include <vector>

#include "modutil.hpp"

#define USAGE \
  "Dump information about module(s) in various module formats.\n\n" \
  "Usage:\n" \
  "  %s [options] [filename.ext...]\n\n" \

static int total_unidentified = 0;


namespace modutil
{
static std::vector<const modutil::loader *> &loaders_vector()
{
  static std::vector<const modutil::loader *> vec{};
  return vec;
}

#define ORDERED(str) \
 if(!strncmp(a->name, str, strlen(str))) return true; \
 else if(!strncmp(b->name, str, strlen(str))) return false;

static bool sort_function(const modutil::loader *a, const modutil::loader *b)
{
  // Sort the "main five" MegaZeux module formats first, followed by everything else alphabetically.
  ORDERED("MOD");
  ORDERED("S3M");
  ORDERED("XM");
  ORDERED("IT");
  ORDERED("GDM");
  return strcmp(a->name, b->name) < 0;
}

static void sort_loaders()
{
  auto &loaders = loaders_vector();
  std::sort(loaders.begin(), loaders.end(), sort_function);
}

modutil::loader::loader(const char *n): name(n)
{
  loaders_vector().push_back(this);
}

static void check_module(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    setvbuf(fp, NULL, _IOFBF, 8192);

    // FIXME standardize message spacing (10 is most common).

    format::line("File", "%s", filename);

    modutil::error err;
    bool has_format = false;

    for(const modutil::loader *loader : loaders_vector())
    {
      err = loader->load(fp);
      if(err == modutil::FORMAT_ERROR)
      {
        rewind(fp);
        continue;
      }

      has_format = true;
      if(err)
        format::error("in loader '%s': %s", loader->name, modutil::strerror(err));
      else
        format::endline();

      break;
    }
    if(!has_format)
    {
      format::error("unknown format.");
      format::endline();
      total_unidentified++;
    }

    fclose(fp);
  }
  else
    format::error("failed to open '%s'.", filename);
}

} /* namespace modutil */


int main(int argc, char *argv[])
{
  bool read_stdin = false;
  modutil::sort_loaders();

  if(!argv || argc < 2)
  {
    const char *name = argv ? argv[0] : "modutil";
    fprintf(stdout, USAGE "%s", name, Config.COMMON_FLAGS);

    fprintf(stdout, "Supported formats:\n");
    for(const modutil::loader *loader : modutil::loaders_vector())
      fprintf(stdout, " * %s\n", loader->name);
    fprintf(stdout, "\n");
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
          modutil::check_module(buffer);

        read_stdin = true;
      }
      continue;
    }
    modutil::check_module(argv[i]);
  }

  for(const modutil::loader *loader : modutil::loaders_vector())
    loader->report();

  if(total_unidentified)
    format::report("Total unidentified", total_unidentified);

  return 0;
}
