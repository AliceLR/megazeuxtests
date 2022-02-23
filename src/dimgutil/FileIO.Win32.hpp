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

#ifndef MZXTEST_DIMGUTIL_FILEIO_WIN32_HPP
#define MZXTEST_DIMGUTIL_FILEIO_WIN32_HPP

#include "FileIO.hpp"
#include "../common.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shlwapi.h>
#include <wchar.h>

/**
 * The wide char conversion functions were added in Windows 2000 and generally
 * the wide versions of the stdio functions just don't appear to work in older
 * OSes (tested Win98 with KernelEx using tdm-gcc 5.1.0).
 */
#if WINVER >= 0x500 /* _WIN32_WINNT_WIN2K */
#define WIDE_PATHS 1
#endif

#ifdef WIDE_PATHS
/**
 * Convert a UTF-8 char string into a UTF-16 wide char string for use with
 * Win32 wide char functions. Returns the length of the output (including the
 * null terminator) or 0 on failure.
 */
static inline int utf8_to_utf16(const char *src, wchar_t *dest, int dest_size)
{
  return MultiByteToWideChar(
    CP_UTF8,
    0,
    (LPCCH)src,
    -1, // Null terminated.
    (LPWSTR)dest,
    dest_size
  );
}

/**
 * Convert a UTF-16 wide char string into a UTF-8 char string for general
 * usage. Returns the length of the output (including the null terminator)
 * or 0 on failure.
 */
static inline int utf16_to_utf8(const wchar_t *src, char *dest, int dest_size)
{
  return WideCharToMultiByte(
    CP_UTF8,
    0,
    (LPCWCH)src,
    -1, // Null terminated.
    (LPSTR)dest,
    dest_size,
    NULL,
    NULL
  );
}
#endif

FILE *FileIO::io_tempfile(char (&dest)[TEMPFILE_SIZE])
{
#ifdef WIDE_PATHS
  wchar_t w_temppath[MAX_PATH];
  wchar_t w_dest[MAX_PATH];
  if(GetTempPathW(arraysize(w_temppath), w_temppath) &&
     GetTempFileNameW(w_temppath, L"mzt", 0, w_dest) &&
     utf16_to_utf8(w_dest, dest, MAX_PATH))
  {
    return _wfopen(w_dest, L"w+b");
  }
#endif

  char temppath[MAX_PATH];
  if(GetTempPath(arraysize(temppath), temppath) &&
     GetTempFileName(temppath, "mzt", 0, dest))
  {
    return fopen(dest, "w+b");
  }
  return nullptr;
}

FILE *FileIO::io_fopen(const char *path, const char *mode)
{
#ifdef WIDE_PATHS
  wchar_t w_path[1024];
  wchar_t w_mode[16];
  if(utf8_to_utf16(path, w_path, arraysize(w_path)) &&
     utf8_to_utf16(mode, w_mode, arraysize(w_mode)))
    return _wfopen(w_path, w_mode);
#endif

  return fopen(path, mode);
}

bool FileIO::io_mkdir(const char *path, int mode)
{
#ifdef WIDE_PATHS
  wchar_t w_path[1024];
  if(utf8_to_utf16(path, w_path, arraysize(w_path)))
    return _wmkdir(w_path) == 0;
#endif

  return mkdir(path) == 0;
}

bool FileIO::io_unlink(const char *path)
{
#ifdef WIDE_PATHS
  wchar_t w_path[1024];
  if(utf8_to_utf16(path, w_path, arraysize(w_path)))
    return _wunlink(w_path) == 0;
#endif

  return unlink(path) == 0;
}

bool FileIO::io_rename(const char *old_path, const char *new_path)
{
#ifdef WIDE_PATHS
  wchar_t w_old_path[1024];
  wchar_t w_new_path[1024];
  if(utf8_to_utf16(old_path, w_old_path, arraysize(w_old_path)) &&
     utf8_to_utf16(new_path, w_new_path, arraysize(w_new_path)))
    return _wrename(w_old_path, w_new_path) == 0;
#endif

  return rename(old_path, new_path) == 0;
}

int FileIO::io_get_file_type(const char *path)
{
  DWORD attr;

#ifdef WIDE_PATHS
  wchar_t w_path[1024];
  if(utf8_to_utf16(path, w_path, arraysize(w_path)))
  {
    attr = GetFileAttributesW(w_path);
  }
  else
#endif
  {
    attr = GetFileAttributes(path);
  }
  if(attr == INVALID_FILE_ATTRIBUTES || !attr)
    return TYPE_UNKNOWN;

  return (attr & FILE_ATTRIBUTE_DIRECTORY) ? TYPE_DIR : TYPE_FILE;
}

static inline bool convert_time(FILETIME *dest, uint64_t file_d, uint32_t file_ns)
{
  SYSTEMTIME stime =
  {
    /* wYear   */ FileInfo::date_year(file_d),
    /* wMonth  */ FileInfo::date_month(file_d),
    0,
    /* wDay    */ FileInfo::date_day(file_d),
    /* wHour   */ FileInfo::time_hours(file_d),
    /* wMinute */ FileInfo::time_minutes(file_d),
    /* wSecond */ FileInfo::time_seconds(file_d),
    /* wMilliseconds */ (WORD)(file_ns / 1000000),
  };
  return SystemTimeToFileTime(&stime, dest);
}

bool FileIO::set_file_times(const FileInfo &info, FILE *fp)
{
  int fd = _fileno(fp);
  if(fd >= 0)
  {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if(h != INVALID_HANDLE_VALUE)
    {
      FILETIME _access;
      FILETIME _create;
      FILETIME _modify;
      FILETIME *access = &_access;
      FILETIME *create = &_create;
      FILETIME *modify = &_modify;
      if(!convert_time(access, info.access_d, info.access_ns))
        access = nullptr;
      if(!convert_time(create, info.create_d, info.create_ns))
        create = nullptr;
      if(!convert_time(modify, info.modify_d, info.modify_ns))
        modify = nullptr;

      return SetFileTime(h, create, access, modify);
    }
  }
  return false;
}

bool FileIO::match_path(const char *path, const char *pattern)
{
#ifdef WIDE_PATHS
  wchar_t wpath[1024];
  wchar_t wpat[1024];

  if(utf8_to_utf16(path, wpath, arraysize(wpath)) &&
     utf8_to_utf16(pattern, wpat, arraysize(wpat)))
  {
    return PathMatchSpecW(wpath, wpat);
  }
#endif

  return PathMatchSpec(path, pattern);
}

#define FILTER_NAME(fn, str) \
 (!strncmp(fn, str, strlen(str)) && (fn[strlen(str)] == '\0' || fn[strlen(str)] == '.'))
#define FILTER_NAME_NUM(fn, str) \
 (!strncmp(fn, str, strlen(str)) && isdigit((int)fn[strlen(str)]) && (fn[strlen(str)+1] == '\0' || fn[strlen(str)+1] == '.'))

bool FileIO::clean_path_token(char *filename)
{
  /**
   * "Do not use the following reserved names for the name of a file:
   *
   * CON, PRN, AUX, NUL, COM1, ... and LPT9.
   *
   * Also avoid these names followed immediately by an extension; for example,
   * NUL.txt is not recommended."
   */
  if(FILTER_NAME(filename, "CON") ||
     FILTER_NAME(filename, "PRN") ||
     FILTER_NAME(filename, "AUX") ||
     FILTER_NAME(filename, "NUL") ||
     FILTER_NAME_NUM(filename, "COM") ||
     FILTER_NAME_NUM(filename, "LPT"))
  {
    filename[2] = '~';
    return true;
  }

  while(*filename)
  {
    switch(*filename)
    {
      case '<':
      case '>':
      case ':':
      case '"':
      case '/':
      case '\\':
      case '|':
      case '?':
      case '*':
        *filename = '_';
        break;
      default:
        if(*filename < 32)
          *filename = '_';
        break;
    }
    filename++;
  }
  return true;
}

#endif /* MZXTEST_DIMGUTIL_FILEIO_WIN32_HPP */
