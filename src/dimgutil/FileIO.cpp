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

#include "FileIO.hpp"

#ifdef _WIN32
#include "FileIO.Win32.hpp"
#else
#include "FileIO.POSIX.hpp"
#endif

FileIO::~FileIO()
{
  if(state != INIT)
  {
    fclose(fp);
    unlink(path);
  }
}

FILE *FileIO::get_file()
{
  if(state == SUCCESS)
    return nullptr;

  if(state == INIT)
  {
    fp = io_tempfile(path);
    if(fp)
      state = OPEN;
  }
  return fp;
}

bool FileIO::commit(const FileInfo &dest, const char *destdir)
{
  // FIXME either split dest name to dir+filename or use destdir+filename.
  // FIXME make the destination dir recursively if it doesn't exist.
  // FIXME unlink the destination file if it does exist.
  // FIXME rename the temporary file to the destination dir+filename, set state to SUCCESS on success, otherwise leave it at OPEN
  // FIXME set file timestamps to match the FileInfo.
  return false;
}
