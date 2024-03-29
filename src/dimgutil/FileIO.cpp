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

#include "FileIO.hpp"
#include "../common.hpp"

#ifdef _WIN32
#include "FileIO.Win32.hpp"
#else
#include "FileIO.POSIX.hpp"
#endif

#if defined(__GNUC__) && __GNUC__ >= 11
#define ANNOYING_WARNING
#endif

#ifdef ANNOYING_WARNING
// GCC can't figure out that the dir separator fix will never actually index buffer[-1] ;-(
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
static bool _io_mkdir_recursive(char (&buffer)[1024])
{
  char *cursor = buffer;
  char *current;

  while((current = path_tokenize(&cursor)))
  {
    // Fix dir separators.
    if(current > buffer)
      current[-1] = DIR_SEPARATOR;

    int res = FileIO::io_get_file_type(buffer);
    if(res == FileIO::TYPE_UNKNOWN)
    {
      if(!FileIO::io_mkdir(buffer, 0755))
        return false;
    }
    else

    if(res == FileIO::TYPE_FILE)
      return false;
  }
  return true;
}
#ifdef ANNOYING_WARNING
#pragma GCC diagnostic pop
#endif

static bool _io_copy_file(const char *in, const char *out)
{
  FILE *a = fopen(in, "rb");
  FILE *b = fopen(out, "wb");
  bool success = false;
  if(a && b)
  {
    uint8_t buffer[8192];
    size_t len;
    bool short_write = false;
    while((len = fread(buffer, 1, sizeof(buffer), a)))
    {
      if(fwrite(buffer, 1, len, b) != len)
      {
        short_write = true;
        break;
      }
    }
    if(!short_write && !ferror(a) && !ferror(b))
      success = true;
  }
  if(a)
    fclose(a);
  if(b)
    fclose(b);

  return success;
}

FileIO::~FileIO()
{
  if(state > INIT && state < SUCCESS)
  {
    fclose(fp);
    unlink(path);
  }
}

FILE *FileIO::get_file()
{
  if(state == INIT)
  {
    fp = io_tempfile(path);
    if(fp)
      state = OPEN;
  }
  return fp;
}

static void apply_destdir(char (&buffer)[1024], const char *name, const char *destdir = nullptr)
{
  if(destdir)
  {
    snprintf(buffer, sizeof(buffer), "%s%c%s", destdir, DIR_SEPARATOR, name);
    path_clean_slashes(buffer);
  }
  else
    snprintf(buffer, sizeof(buffer), "%s", name);
}

bool FileIO::commit(const FileInfo &dest, const char *destdir)
{
  if(state != OPEN)
    return false;

  char buffer[1024];
  char *current;
  char *cursor;
  char *last_separator = nullptr;

  apply_destdir(buffer, dest.name(), destdir);

  // 1. Clean illegal characters from the destination path.
  cursor = buffer;
  while((current = path_tokenize(&cursor)))
  {
    clean_path_token(current);

    // Fix dir separators.
    if(current > buffer)
    {
      last_separator = current - 1;
      *last_separator = DIR_SEPARATOR;
    }
  }

  // 2. If there is a parent, does the parent exist?
  bool target_exists = false;
  int res;
  if(last_separator)
  {
    *last_separator = '\0';
    res = io_get_file_type(buffer);
    *last_separator = DIR_SEPARATOR;

    if(res == TYPE_DIR)
    {
      // Does the target exist?
      res = io_get_file_type(buffer);
      if(res == TYPE_DIR)
        return false;

      if(res == TYPE_FILE)
        target_exists = true;
    }
    else

    if(res == TYPE_UNKNOWN)
    {
      // Attempt to mkdir recursively.
      *last_separator = '\0';
      if(!_io_mkdir_recursive(buffer))
        return false;
      *last_separator = DIR_SEPARATOR;
    }
    else
      return false;
  }
  else
  {
    // Does the target exist?
    res = io_get_file_type(buffer);
    if(res == TYPE_DIR)
      return false;

    if(res == TYPE_FILE)
      target_exists = true;
  }

  // 3. Unlink target if it exists.
  if(target_exists)
    if(!io_unlink(buffer))
      return false;

  // 4. Set timestamps and close.
  // The file can't be renamed while it's still open (at least in Windows).
  set_file_times(dest, fp);
  fclose(fp);
  fp = nullptr;

  // 5. Rename tempfile to the target.
  if(!io_rename(this->path, buffer))
  {
    // Fedora doesn't allow rename(2) on owned files in /tmp, but seems
    // fine with unlink(2). Presumably this made sense to someone...?
    bool success = _io_copy_file(this->path, buffer);
    io_unlink(this->path);

    if(!success)
    {
      // oopsie woopsie!
      state = FileIO::INIT;
      return false;
    }
  }

  state = SUCCESS;
  return true;
}

bool FileIO::create_directory(const char *filename, const char *destdir)
{
  char buffer[1024];
  apply_destdir(buffer, filename, destdir);

  return _io_mkdir_recursive(buffer);
}
