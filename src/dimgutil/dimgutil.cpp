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

#include <memory>

#include "../common.hpp"
#include "../format.hpp"

#include "DiskImage.hpp"


enum disk_op
{
  OP_INFO,
  OP_LIST,
  OP_TEST,
  OP_EXTRACT,
  NUM_DISK_OPS
};

static constexpr char op_chars[NUM_DISK_OPS] =
{
  'i', 'l', 't', 'x',
};

#ifdef LIBFUZZER_FRONTEND
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  // FIXME!! needs quiet mode, configury, and to split some stuff out of main. don't care enough right now :(
  return 0;
}

#define main _main
static __attribute__((unused))
#endif

int main(int argc, char *argv[])
{
  if(argc < 3)
  {
    O_("Usage: dimgutil [i|l|t|x] filename.ext [...]\n");
    return 0;
  }

  int op;
  for(op = 0; op < arraysize(op_chars); op++)
  {
    if(tolower(argv[1][0]) == op_chars[op] && argv[1][1] == '\0')
      break;
  }
  if(op >= arraysize(op_chars))
  {
    format::error("invalid operation '%s'", argv[1]);
    return -1;
  }

  char *filename = argv[2];

  format::line("File", "%s", filename);

  FILE *fp = fopen(filename, "rb");
  if(!fp)
  {
    format::error("error opening file");
    return -1;
  }
  long file_length = get_file_length(fp);
  std::unique_ptr<DiskImage> disk(DiskImageLoader::TryLoad(fp, file_length));
  fclose(fp);

  if(!disk || disk->error_state)
  {
    format::error("error loading image");
    return -1;
  }

  switch(op)
  {
    case OP_INFO:
      disk->PrintSummary();
      break;

    case OP_LIST:
    {
      // TODO filter
      char *base = (argc > 3) ? argv[3] : nullptr;
      FileList list;

      disk->PrintSummary();
      disk->Search(list, base, true);

      fprintf(stderr, "\nListing '%s':\n\n", base ? base : "");
      FileInfo::print_header();
      for(FileInfo &f : list)
        f.print();

      fprintf(stderr, "\n  Total: %zu\n", list.size());
      break;
    }

    case OP_TEST:
    {
      // FIXME need mostly the same features as OP_EXTRACT
      char *base = (argc > 3) ? argv[3] : nullptr;
      size_t failed = 0;
      size_t ok = 0;
      FileList list;

      disk->PrintSummary();
      disk->Search(list, base, true);

      fprintf(stderr, "\nTesting '%s':\n\n", base ? base : "");
      FileInfo::print_header();
      for(FileInfo &f : list)
        f.print();

      for(FileInfo &f : list)
      {
        if(!disk->Test(f))
        {
          fprintf(stderr, "  Error: test failed for '%s'.\n", f.name());
          failed++;
        }
        else
          ok++;
      }

      fprintf(stderr, "\n  OK: %zu  Failed: %zu  Total: %zu\n", ok, failed, list.size());
      break;
    }

    case OP_EXTRACT:
    {
      // TODO filter
      // TODO destination directory
      // FIXME
      //char *base = (argc > 3) ? argv[3] : nullptr;
      char *base = nullptr;
      char *destdir = (argc > 3) ? argv[3] : nullptr;
      FileList list;

      disk->PrintSummary();
      disk->Search(list, base, true);

      fprintf(stderr, "\nExtracting '%s':\n\n", base ? base : "");
      FileInfo::print_header();
      for(FileInfo &f : list)
        f.print();

      for(FileInfo &f : list)
        if(!disk->Extract(f, destdir))
          fprintf(stderr, "  Error: failed to extract '%s'.\n", f.name());

      fprintf(stderr, "\n  Total: %zu\n", list.size());
    }
  }
  format::endline();

  return 0;
}
