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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "DiskImage.hpp"
#include "../format.hpp"


static constexpr int SMALL_SECTOR = 256;
static constexpr int LARGE_SECTOR = 1024;

enum ADFS_type
{
  NOT_ADFS,
  ADFS_S_160K,
  ADFS_M_320K,
  ADFS_L_640K,
  ADFS_D_800K,
  ADFS_E_800K,
  ADFS_F_1600K,
  ADFS_G_3200K,
  NUM_ADFS_TYPES
};

struct ADFS_type_spec
{
  const char *name;
  const char *media;
  unsigned int size;
  unsigned int num_sectors;
  unsigned int bytes_per_sector;
};

static constexpr ADFS_type_spec ADFS_TYPES[NUM_ADFS_TYPES] =
{
  {},
  { "ADFS-S", "5.25\" (1 side, 40 tracks, 16 sectors)",   160,  640, SMALL_SECTOR },
  { "ADFS-M", "5.25\" (1 side, 80 tracks, 16 sectors)",   320, 1280, SMALL_SECTOR },
  { "ADFS-L", "5.25\" (2 sides, 80 tracks, 16 sectors)",  640, 2560, SMALL_SECTOR },
  { "ADFS-D", "3.5\" (2 sides, 80 tracks, 5 sectors)",    800,  800, LARGE_SECTOR },
  { "ADFS-E", "3.5\" (2 sides, 80 tracks, 5 sectors)",    800,  800, LARGE_SECTOR },
  { "ADFS-F", "3.5\" (2 sides, 80 tracks, 10 sectors)",  1600, 1600, LARGE_SECTOR },
  { "ADFS-G", "3.5\" (2 sides, 80 tracks, 20 sectors)",  1600, 1600, LARGE_SECTOR },
};

static constexpr char HUGO[] = "Hugo";
static constexpr char NICK[] = "Nick";

struct ADFS_map
{
  ADFS_type type;

  /* Old map sector 0. */
  uint32_t free_start[82]; // 24-bit
  /* uint8_t  reserved; */
  char     name0[5];
  uint32_t num_sectors;    // 24-bit
  uint8_t  checksum0;
  /* Old map sector 1. */
  uint32_t free_len[82];   // 24-bit
  char     name1[5];
  uint16_t disk_id;
  uint8_t  boot_option;
  uint8_t  free_end;
  uint8_t  checksum1;

  /* New map. */
  uint8_t  check_zone;
  uint16_t first_free;
  uint8_t  check_cross;
  uint8_t  log2_sector_size;
  uint8_t  sectors_per_track;
  uint8_t  heads;
  uint8_t  density;
  uint8_t  id_length;
  uint8_t  log2_bytes_per_map_bit;
  uint8_t  skew;
  /* uint8_t boot_option; */
  uint8_t  low_sector;
  uint8_t  num_zones;
  uint16_t zone_spare;
  uint32_t root_address; // 24-bit
  uint32_t disk_size_in_bytes;
  /* uint16_t disk_id; */
  char     disk_name[10];
  uint32_t disk_type;
  uint32_t disk_size_in_bytes_hi;
  uint8_t  log2_share_size;
  uint8_t  big_flag;
  uint8_t  num_zones2;
  /* uint8_t reserved; */
  uint32_t format_version; // 24-bit
  /* root_size ???? type??? */
};


class ADFSImage: public DiskImage
{
  const ADFS_type_spec &adfs;
  ADFS_map map;

public:
  ADFSImage(ADFS_type _t, ADFS_map &_map):
   DiskImage::DiskImage(ADFS_TYPES[_t].name, ADFS_TYPES[_t].media), adfs(ADFS_TYPES[_t]), map(_map) {}
  virtual ~ADFSImage() {}

  /* "Driver" implemented functions. */
  virtual bool PrintSummary() const override;
  virtual bool Search(FileList &dest, const FileInfo &filter, uint32_t filter_flags,
   const char *base, bool recursive = false) const override;
  virtual bool Test(const FileInfo &file) override;
  virtual bool Extract(const FileInfo &file, const char *destdir = nullptr) override;
};

bool ADFSImage::PrintSummary() const
{
  format::line("Type",     "%s", type);
  format::line("Media",    "%s", media);
  format::line("Size",     "%u", adfs.size * 1024);
  format::line("Sectors",  "%u", adfs.num_sectors);
  format::line("SectorSz", "%u", adfs.bytes_per_sector);
  return true;
}

bool ADFSImage::Search(FileList &dest, const FileInfo &filter, uint32_t filter_flags,
 const char *base, bool recursive) const
{
  // FIXME
  return false;
}

bool ADFSImage::Test(const FileInfo &file)
{
  // FIXME
  return true;
}

bool ADFSImage::Extract(const FileInfo &file, const char *destdir)
{
  // FIXME
  return false;
}


class ADFSLoader: public DiskImageLoader
{
  ADFS_type init_old_map(FILE *fp, int num_sides, ADFS_map &map) const
  {
    // FIXME read more here
    // Get real number of sectors.
    if(fseek(fp, 0xfc, SEEK_SET))
      return NOT_ADFS;

    map.num_sectors = fget_u24be(fp);

    if(num_sides > 1)
      return ADFS_L_640K;

    if(map.num_sectors > 640)
      return ADFS_M_320K;

    return ADFS_S_160K;
  }

  ADFS_type init_new_map(FILE *fp, ADFS_map &map) const
  {
    // FIXME read literally anything here
    return ADFS_E_800K;
  }

  ADFS_type identify(FILE *fp, ADFS_map &map) const
  {
    char magic[4];
    char magic2[4];

    // One sided ADFS-S and ADFS-M should have a "Hugo" directory magic at byte 1 of the 2nd sector.
    if(fseek(fp, SMALL_SECTOR * 2 + 1, SEEK_SET))
      return NOT_ADFS;

    if(!fread(magic, 4, 1, fp))
      return NOT_ADFS;

    if(!memcmp(magic, HUGO, 4))
      return init_old_map(fp, 1, map);

    // Two sided ADFS-L should have a "Hugo" directory magic at byte 1 of the 2nd sector on either side.
    // Sides are interleaved. For large sector disks, this corresponds to the start of the second side.
    if(fseek(fp, SMALL_SECTOR * 2 * 2 + 1, SEEK_SET))
      return NOT_ADFS;

    if(!fread(magic2, 4, 1, fp))
      return NOT_ADFS;

    if(!memcmp(magic2, HUGO, 4))
      return init_old_map(fp, 2, map);

    // Two sided volumes with large sectors should have four NUL bytes at the 2nd (256 byte)
    // sector of either side, corresponding to the position read from the first "magic".
    // "Hugo" or "Nick" will be at byte 1 of the 4th (256 byte) sector on either side.
    if(memcmp(magic, "\0\0\0\0", 4))
      return NOT_ADFS;

    if(fseek(fp, SMALL_SECTOR * 2 * 4 + 1, SEEK_SET))
      return NOT_ADFS;

    if(!fread(magic, 4, 1, fp))
      return NOT_ADFS;

    if(!memcmp(magic, HUGO, 4))
      return ADFS_D_800K;

    if(!memcmp(magic, NICK, 4))
      return init_new_map(fp, map);

    return NOT_ADFS;
  }

public:
  virtual DiskImage *Load(FILE *fp, long file_length) const override
  {
    ADFS_map map;
    ADFS_type type = identify(fp, map);
    if(type == NOT_ADFS)
      return nullptr;

    return new ADFSImage(type, map);
  }
};

static const ADFSLoader loader;
