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
  OP_EXTRACT,
  NUM_DISK_OPS
};

static constexpr char op_chars[NUM_DISK_OPS] =
{
  'i', 'l', 'x',
};

int main(int argc, char *argv[])
{
  if(argc < 3)
  {
    O_("Usage: dimgutil [i|l|x] filename.ext [...]\n");
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
      for(FileInfo &f : list)
        f.print();

      fprintf(stderr, "\n  Total: %zu\n", list.size());
      break;
    }

    case OP_EXTRACT:
    {
      // TODO filter
      // TODO destination directory
      char *base = (argc > 3) ? argv[3] : nullptr;
      FileList list;

      disk->PrintSummary();
      disk->Search(list, base, true);

      fprintf(stderr, "\nExtracting '%s':\n\n", base ? base : "");
      for(FileInfo &f : list)
      {
        f.print();
        if(!disk->Extract(f))
        {
          fprintf(stderr, "  Error: failed to extract '%s'.\n", f.name());
          break;
        }
      }

      fprintf(stderr, "\n  Total: %zu\n", list.size());
    }
  }

  return 0;
}
