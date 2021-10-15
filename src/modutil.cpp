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

#include <ctype.h>
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
char loaded_mod_magic[4];

static std::vector<const modutil::loader *> &loaders_vector()
{
  static std::vector<const modutil::loader *> vec{};
  return vec;
}

#define ORDERED(str) \
 if(!strcmp(a->ext, str)) return true; \
 else if(!strcmp(b->ext, str)) return false;

static bool sort_function(const modutil::loader *a, const modutil::loader *b)
{
  // Sort the "main five" MegaZeux module formats first, followed by everything else alphabetically.
  ORDERED("MOD");
  ORDERED("S3M");
  ORDERED("XM");
  ORDERED("IT");
  ORDERED("GDM");
  int cmp = strcmp(a->ext, b->ext);
  return cmp ? cmp < 0 : strcmp(a->name, b->name) < 0;
}

static void sort_loaders()
{
  auto &loaders = loaders_vector();
  std::sort(loaders.begin(), loaders.end(), sort_function);
}

modutil::loader::loader(const char *e, const char *n): ext(e), name(n)
{
  loaders_vector().push_back(this);
}

static void check_module(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    setvbuf(fp, NULL, _IOFBF, 8192);
    loaded_mod_magic[0] = '\0';

    format::line("File", "%s", filename);

    modutil::error err;
    bool has_format = false;
    long file_length = get_file_length(fp);

    for(const modutil::loader *loader : loaders_vector())
    {
      err = loader->load(fp, file_length);
      if(err == modutil::FORMAT_ERROR)
      {
        rewind(fp);
        continue;
      }

      has_format = true;
      if(err)
        format::error("in loader '%s': %s", loader->name, modutil::strerror(err));

      format::endline();
      break;
    }
    if(!has_format)
    {
      format::error("unknown format.");
      total_unidentified++;

      /* The most common reason for an unsupported format in a folder containing
       * mostly a supported format is an unknown MOD magic, so print the potential magic. */
      bool print_magic = true;
      bool print_hex = false;
      for(char c : loaded_mod_magic)
      {
        if(!c)
          print_magic = false;

        if(!isprint(c))
          print_hex = true;
      }

      if(print_magic)
      {
        if(print_hex)
        {
          format::line("", "MOD magic?: %02Xh %02Xh %02Xh %02Xh",
           (uint8_t)loaded_mod_magic[0], (uint8_t)loaded_mod_magic[1],
           (uint8_t)loaded_mod_magic[2], (uint8_t)loaded_mod_magic[3]);
        }
        else
          format::line("", "MOD magic?: '%4.4s'", loaded_mod_magic);
      }
      format::endline();
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
      fprintf(stdout, " * %-3.3s : %s\n", loader->ext, loader->name);
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
