/**
 * Copyright (C) 2021 Lachesis <petrifiedrowan@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

static int total_identified = 0;
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

modutil::loader::loader(const char *e, const char *t, const char *n): ext(e), tag(t), name(n)
{
  loaders_vector().push_back(this);
}

static bool is_loader_filtered(const modutil::loader *loader)
{
  if(Config.num_format_filters)
  {
    for(int i = 0; i < Config.num_format_filters; i++)
    {
      if(!strcasecmp(loader->ext, Config.format_filter[i]) ||
         !strcasecmp(loader->tag, Config.format_filter[i]))
        return false;
    }
    return true;
  }
  return false;
}

static void check_module(FILE *fp)
{
  if(fp)
  {
    loaded_mod_magic[0] = '\0';

    modutil::error err;
    bool has_format = false;
    long file_length = get_file_length(fp);

    for(const modutil::loader *loader : loaders_vector())
    {
      if(is_loader_filtered(loader))
        continue;

      err = loader->load(fp, file_length);
      if(err == modutil::FORMAT_ERROR)
      {
        rewind(fp);
        continue;
      }

      has_format = true;
      total_identified++;
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
  }
}

static void check_module(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    format::line("File", "%s", filename);

    setvbuf(fp, NULL, _IOFBF, 8192);
    check_module(fp);
    fclose(fp);
  }
  else
    format::error("failed to open '%s'.", filename);
}

} /* namespace modutil */


#ifdef LIBFUZZER_FRONTEND
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  Config.dump_samples = true;
  Config.dump_patterns = true;
  Config.dump_pattern_rows = true;
  Config.dump_descriptions = true;
  Config.quiet = true;

  FILE *fp = fmemopen(const_cast<uint8_t *>(data), size, "rb");
  if(fp)
  {
    modutil::check_module(fp);
    fclose(fp);
  }
  return 0;
}

#define main _main
static __attribute__((unused))
#endif

int main(int argc, char *argv[])
{
  bool read_stdin = false;
  modutil::sort_loaders();

  if(!argv || argc < 2)
  {
    const char *name = argv ? argv[0] : "modutil";
    fprintf(stdout, USAGE "%s", name, Config.COMMON_FLAGS);

    fprintf(stdout, "Supported formats:\n\n");
    fprintf(stdout, "   Ext : Tag    : Description\n");
    fprintf(stdout, "   --- : ------ : -----------\n");
    for(const modutil::loader *loader : modutil::loaders_vector())
      fprintf(stdout, " * %-3.3s : %-6.6s : %s\n", loader->ext, loader->tag, loader->name);
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

  return (total_identified == 0);
}
