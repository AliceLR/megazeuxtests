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
#include <string.h>
#include <memory>
#include <vector>

#include "../common.hpp"
#include "../format.hpp"

#include "DiskImage.hpp"

static constexpr uint32_t NO_CLUSTER = UINT32_MAX;

enum FAT_media
{
  F12_8IN_250K             = 0xe5,
  F12_5IN_720K             = 0xed,
  F12_DR_DOS_PARTITION     = 0xee,
  F12_DR_DOS_SUPERFLOPPY   = 0xef,
  F12_CUSTOM_3IN_1_44M     = 0xf0,
  F12_DOUBLE_DENSITY       = 0xf4,
  F12_ALTOS_FIXED          = 0xf5,
  F12_FIXED                = 0xf8,
  F12_3IN                  = 0xf9,
  F12_3IN_320K             = 0xfa,
  F12_3IN_640K             = 0xfb,
  F12_5IN_180K             = 0xfc,
  F12_5IN_360K_OR_8IN_500K = 0xfd,
  F12_5IN_160K_OR_8IN      = 0xfe,
  F12_5IN_320K             = 0xff,
};

struct media
{
  const char *format;
  size_t size;
};

static const char *FAT_media_str(int media)
{
  switch(media)
  {
    case F12_8IN_250K:             return "8\" (250k)";
    case F12_5IN_720K:             return "5.25\" (720k)";
    case F12_DR_DOS_PARTITION:     return "DR DOS custom";
    case F12_DR_DOS_SUPERFLOPPY:   return "DR DOS superfloppy";
    case F12_CUSTOM_3IN_1_44M:     return "3.5\" superfloppy";
    case F12_DOUBLE_DENSITY:       return "double density";
    case F12_ALTOS_FIXED:          return "Altos fixed partition";
    case F12_FIXED:                return "fixed partition";
    case F12_3IN:                  return "3.5\" (720k, 1440k) or 5.25\" (1200k)";
    case F12_3IN_320K:             return "3.5\"/5.25\" (320k)";
    case F12_3IN_640K:             return "3.5\"/5.25\" (640k)";
    case F12_5IN_180K:             return "5.25\" (180k)";
    case F12_5IN_360K_OR_8IN_500K: return "5.25\" (360k) or 8.0\" (500.5k)";
    case F12_5IN_160K_OR_8IN:      return "5.25\" (160k) or 8\" (250.25k, 1232k)";
    case F12_5IN_320K:             return "5.25\" (320k)";
  }
  return "unknown";
}

struct FAT_bios
{
  /* Common structure. */
  /* 11 */ uint16_t bytes_per_sector; /* Logical */
  /* 13 */ uint8_t  num_sectors_per_cluster; /* Logical */
  /* 14 */ uint16_t reserved_sectors; /* Logical */
  /* 16 */ uint8_t  num_fats;
  /* 17 */ uint16_t num_root_entries;
  /* 19 */ uint16_t num_sectors; /* Logical */
  /* 21 */ uint8_t  media_descriptor;
  /* 22 */ uint16_t num_sectors_per_fat; /* Logical */

  /* DOS 3.0 fields. */
  /* 24 */ uint16_t num_phys_sectors_per_track;
  /* 26 */ uint16_t num_phys_heads;
  /* 28 */ uint16_t num_hidden_sectors;

  /* DOS 3.2 fields. */
  /* 30 */ uint16_t num_sectors_total; /* Logical + hidden. */
  /* 32 */

  /* DOS 3.31 fields. */
  /* 28 */ uint32_t num_hidden_sectors_32;
  /* 32 */ uint32_t num_sectors_total_32;
  /* 36 */
};

static int bios_2_0(FAT_bios &bios, const uint8_t *sector)
{
  bios.bytes_per_sector        = mem_u16le(sector + 11);
  bios.num_sectors_per_cluster = sector[13];
  bios.reserved_sectors        = mem_u16le(sector + 14);
  bios.num_fats                = sector[16];
  bios.num_root_entries        = mem_u16le(sector + 17);
  bios.num_sectors             = mem_u16le(sector + 19);
  bios.media_descriptor        = sector[21];
  bios.num_sectors_per_fat     = mem_u16le(sector + 22);
  return 0;
}

static int bios_3_0(FAT_bios &bios, const uint8_t *sector)
{
  bios_2_0(bios, sector);

  bios.num_phys_sectors_per_track = mem_u16le(sector + 24);
  bios.num_phys_heads             = mem_u16le(sector + 26);
  bios.num_hidden_sectors         = mem_u16le(sector + 28);
  return 0;
}

struct FAT_entry
{
  enum attributes
  {
    READ_ONLY    = (1<<0),
    HIDDEN       = (1<<1),
    SYSTEM       = (1<<2),
    VOLUME_LABEL = (1<<3),
    DIRECTORY    = (1<<4),
    ARCHIVE      = (1<<5),
    DEVICE       = (1<<6),

    LFN          = READ_ONLY | HIDDEN | SYSTEM | VOLUME_LABEL,
  };

  /*  0 name */
  /*  8 ext */
  /* 11 attributes */
  /* 12 vfat */
  /* 13 create_time_fine (10ms) */
  /* 14 create_time (2s) */
  /* 16 create_date */
  /* 18 access_date */
  /* 20 cluster_hi */
  /* 22 modify_time */
  /* 24 modify_date */
  /* 26 cluster  */
  /* 28 size */
  /* 32 */
  uint8_t data[32];

  void name(char *dest, size_t dest_len) const
  {
    char *buffer = dest;
    char tmp[13];
    size_t i;

    if(dest_len < 13)
      buffer = tmp;

    for(i = 0; i < 8; i++)
    {
      if(data[i] == ' ')
        break;

      buffer[i] = data[i];
    }
    if(data[8] != ' ')
    {
      buffer[i++] = '.';
      for(size_t j = 0; j < 3; j++, i++)
      {
        if(data[8 + j] == ' ')
          break;

        buffer[i] = data[8 + j];
      }
    }
    buffer[i] = '\0';
    if(buffer == tmp)
      snprintf(dest, dest_len, "%s", tmp);
  }

  int name_cmp(const char *filename) const
  {
    char tmp[13];
    name(tmp, sizeof(tmp));
    return strcasecmp(tmp, filename);
  }

  uint8_t  attributes() const { return data[11]; }
  uint8_t  vfat() const { return data[12]; }
  uint8_t  create_time_fine() const { return data[13]; }
  uint16_t create_time() const { return mem_u16le(data + 14); }
  uint16_t create_date() const { return mem_u16le(data + 16); }
  uint16_t access_date() const { return mem_u16le(data + 18); }
  uint16_t cluster_hi() const { return mem_u16le(data + 20); }
  uint16_t modify_time() const { return mem_u16le(data + 22); }
  uint16_t modify_date() const { return mem_u16le(data + 24); }
  uint16_t cluster() const { return mem_u16le(data + 26); }
  uint32_t size() const { return mem_u32le(data + 28); }

  uint32_t cluster32() const { return (cluster_hi() << 16UL) | cluster(); }

  bool exists() const
  {
    if(!data[0] || data[0] == 0xE5)
      return false;

    // Is this a VFAT LFN entry?
    if((attributes() & LFN) == LFN)
      return false;

    return true;
  }

  int fileinfo_type() const
  {
    uint8_t attr = attributes();
    int type = 0;
    if((attr & LFN) == LFN)
      return FileInfo::IS_LFN;
    if(attr & DIRECTORY)
      type |= FileInfo::IS_DIRECTORY;
    if(attr & VOLUME_LABEL)
      type |= FileInfo::IS_VOLUME;
    if(attr & DEVICE)
      type |= FileInfo::IS_DEVICE;
    if(!type)
      type |= FileInfo::IS_REG;

    return type;
  }
};

struct MS_DOS_FAT12_boot
{
  /*   0 */ uint8_t  jump[3];
  /*   3 */ char     oem[8];
  /*  11 */ FAT_bios bios;
  /*  23 */ uint8_t  priv[485]; /* May be smaller depending on the BIOS size. */
  /* 509 */ uint8_t  drive_number;
  /* 510 */ uint16_t signature; /* 0x55 0xAA */
  /* 512 */
};

struct MSX_DOS_FAT12_boot
{
  /*   0 */ uint8_t  jump[3];
  /*   3 */ uint8_t  oem[8];
  /*  11 */ uint8_t  priv[499];
  /* 510 */ uint16_t signature;
  /* 512 */
};

struct AtariST_FAT12_boot
{
  /*   0 */ uint16_t jump;
  /*   2 */ uint8_t  oem[6];
  /*   8 */ uint8_t  serial[3];
  /*  11 */ FAT_bios bios; /* Always DOS 3.0 format i.e. length is 19. */
  /*  30 */ uint8_t  priv[480];
  /* 510 */ uint16_t checksum;
  /* 512 */
};


class FAT_image: public DiskImage
{
public:
  FAT_bios bios;
  char oem[32];

  uint32_t **fat = nullptr;
  size_t fat_alloc = 0;
  size_t fat_entries = 0;
  size_t size = 0;
  uint32_t dir_entries_per_cluster;
  uint32_t end_of_chain;

  uint8_t *data_area = nullptr;
  size_t data_area_size = 0;
  size_t root_size = 0;
  size_t root_sectors = 0;

  FAT_image(const char *_type, const char *_media, const FAT_bios &_bios):
   DiskImage::DiskImage(_type, _media), bios(_bios)
  {
    oem[0] = '\0';
  }

  FAT_image(const char *_type, const FAT_bios &_bios): FAT_image(_type, nullptr, _bios) {}

  ~FAT_image()
  {
    if(fat)
    {
      for(size_t i = 0; i < fat_alloc; i++)
        delete[] fat[i];
      delete[] fat;
    }
    delete[] data_area;
  }


  /* "Driver" functions. */
  virtual bool PrintSummary() const override;
  virtual bool Search(FileList &dest, const FileInfo &filter, uint32_t filter_flags,
   const char *base, bool recursive) const override;
  virtual bool Test(const FileInfo &file) override;
  virtual bool Extract(const FileInfo &file, const char *destdir = nullptr) override;


  void search_r(FileList &dest, const FileInfo &filter, uint32_t filter_flags,
   const char *base, uint32_t cluster, bool recursive) const;

  uint32_t next_cluster_id(uint32_t cluster) const
  {
    if(error_state)
      return NO_CLUSTER;

    if(cluster == 0 && bios.num_root_entries)
      return NO_CLUSTER;

    if(cluster >= 2 && cluster < fat_entries)
    {
      // TODO check end_of_chain to determine valid end of file vs error
      uint32_t next = fat[0][cluster];
      if(next >= 2 && next < fat_entries)
        return next;
    }
    return NO_CLUSTER;
  }

  const uint8_t *get_cluster(uint32_t cluster) const
  {
    if(error_state)
      return nullptr;

    // Special case--root cluster.
    if(cluster == 0 && bios.num_root_entries)
      return data_area;

    if(cluster >= 2)
    {
      size_t bytes_per_cluster = bios.num_sectors_per_cluster * bios.bytes_per_sector;
      size_t root_offset = root_sectors * bios.bytes_per_sector;
      size_t offset = (cluster - 2) * bytes_per_cluster;

      if(offset + bytes_per_cluster <= data_area_size - root_size)
        return data_area + offset + root_offset;
    }
    return nullptr;
  }

  FAT_entry *get_entry_in_directory(uint32_t directory, const char *name) const;

  FAT_entry *get_entry(uint32_t base, const char *path, uint32_t *parent_cluster = nullptr) const
  {
    char buffer[1024];
    uint32_t parent = base;
    FAT_entry *r = nullptr;

    snprintf(buffer, sizeof(buffer), "%s", path);
    path_clean_slashes(buffer);

    char *cursor = buffer;
    char *current;
    while((current = path_tokenize(&cursor)))
    {
      if(r)
      {
        if(!(r->attributes() & FAT_entry::DIRECTORY))
          return nullptr;

        parent = r->cluster(); // TODO cluster32()
      }

      r = get_entry_in_directory(parent, current);
    }

    if(parent_cluster)
      *parent_cluster = parent;

    return r;
  }

  void print_summary() const
  {
    format::line("Type",     "%s", type);
    format::line("Media",    "%s (0x%02x)", media, bios.media_descriptor);
    format::line("Size",     "%zu", size);
    format::line("OEM",      "%s", oem);
    format::line("Sectors",  "%u", bios.num_sectors);
    format::line("SectorSz", "%u", bios.bytes_per_sector);
    format::line("Clusters", "%u", bios.num_sectors_per_cluster ? bios.num_sectors / bios.num_sectors_per_cluster : 0);
    format::line("ClustrSz", "%u", bios.num_sectors_per_cluster * bios.bytes_per_sector);
    format::line("FATs",     "%u", bios.num_fats);
    format::line("Sect/FAT", "%u", bios.num_sectors_per_fat);
    format::line("Reserved", "%u", bios.reserved_sectors);
    format::line("RootSz",   "%u (%u sectors)", bios.num_root_entries * 32, bios.num_root_entries * 32 / bios.bytes_per_sector);
  }

  void print_FATs() const
  {
    format::line();
    for(size_t i = 0; i < fat_alloc; i++)
    {
      char label[9];
      snprintf(label, sizeof(label), "FAT.%zx", i);
      format::orders(label, fat[i], fat_entries);
    }
  }

protected:
  void init_FAT()
  {
    fat_alloc = bios.num_fats;
    fat = new uint32_t *[fat_alloc]{};
    for(size_t i = 0; i < fat_alloc; i++)
      fat[i] = new uint32_t[fat_entries]{};

    init_media();
    dir_entries_per_cluster = bios.bytes_per_sector * bios.num_sectors_per_cluster / 32;
  }

  void init_media()
  {
    static const char _3IN[] = "3.5\"";
    static const char _5IN[] = "5.25\"";
    static const char _8IN[] = "8\"";
    static const char _3IN_OR_5IN[] = "3.5\" or 5.25\"";

    if(!media)
    switch(bios.media_descriptor)
    {
      case F12_8IN_250K:
        if(size == 250*1024)
          media = _8IN;
        break;

      case F12_5IN_720K:
        if(size == 720*1024)
          media = _5IN;
        break;

      case F12_3IN:
        if(size == 720*1024 || size == 1440*1024)
          media = _3IN;
        else
        if(size == 1200*1024)
          media = _5IN;
        break;

      case F12_3IN_320K:
        if(size == 320*1024)
          media = _3IN_OR_5IN;
        break;

      case F12_3IN_640K:
        if(size == 640*1024)
          media = _3IN_OR_5IN;
        break;

      case F12_5IN_180K:
        if(size == 180*1024)
          media = _5IN;
        break;

      case F12_5IN_360K_OR_8IN_500K:
        if(size == 360*1024)
          media = _5IN;
        else
        if(size == 500*1024+512)
          media = _8IN;
        break;

      case F12_5IN_160K_OR_8IN:
        if(size == 160*1024)
          media = _5IN;
        else
        if(size == 250*1024+256 || size == 1232*1024)
          media = _8IN;
        break;

      case F12_5IN_320K:
        if(size == 320*1024)
          media = _5IN;
        break;
    }

    // Default.
    if(!media)
      media = FAT_media_str(bios.media_descriptor);
  }
};


class FAT_entry_iterator
{
  const FAT_image &disk;

  uint32_t cluster; // 0=root
  unsigned int cluster_entries; // use num_root_entries if root
  FAT_entry *cur = nullptr;
  FAT_entry *cur_end = nullptr;

public:
  FAT_entry_iterator(const FAT_image &_disk, uint32_t _cluster, unsigned int _cluster_entries):
   disk(_disk), cluster(_cluster), cluster_entries(_cluster_entries)
  {
    const uint8_t *data = disk.get_cluster(cluster);
    if(data)
    {
      cur = (FAT_entry *)data;
      cur_end = cur + cluster_entries;
    }
  }

  const FAT_entry_iterator &begin() const
  {
    return *this;
  }

  // Note: requires C++17. for() will use operator FAT_entry * to compare.
  // Doing the iteration like this is cleaner and also prevents null references.
  FAT_entry *end() const
  {
    return nullptr;
  }

  const FAT_entry_iterator &operator++() // prefix.
  {
    if(cur)
    {
      cur++;
      if(cur >= cur_end && cluster != 0)
      {
        uint32_t next = disk.next_cluster_id(cluster);
        if(next != NO_CLUSTER)
        {
          const uint8_t *data = disk.get_cluster(cluster);
          if(data)
          {
            cluster = next;
            cur = (FAT_entry *)data;
            cur_end = cur + cluster_entries;
          }
        }
      }
    }
    return *this;
  }

  operator FAT_entry *() const
  {
    return cur && cur < cur_end && cur->data[0] ? cur : nullptr;
  }
};


FAT_entry *FAT_image::get_entry_in_directory(uint32_t directory, const char *name) const
{
  size_t entries = directory ? dir_entries_per_cluster : bios.num_root_entries;

  for(FAT_entry &e : FAT_entry_iterator(*this, directory, entries))
  {
    // TODO: LFN
    if(e.name_cmp(name) == 0)
      return &e;
  }
  return nullptr;
}


bool FAT_image::PrintSummary() const
{
  if(error_state)
    return false;

  print_summary();
  return true;
}

bool FAT_image::Search(FileList &dest, const FileInfo &filter, uint32_t filter_flags,
 const char *base, bool recursive) const
{
  if(error_state)
    return false;

  uint32_t directory = 0;
  if(base && base[0])
  {
    FAT_entry *t = get_entry(0, base);
    if(!t || !(t->attributes() & FAT_entry::DIRECTORY))
      return false;

    directory = t->cluster();
  }
  else
    base = "";

  search_r(dest, filter, filter_flags, base, directory, recursive);
  return true;
}

void FAT_image::search_r(FileList &dest, const FileInfo &filter, uint32_t filter_flags,
 const char *base, uint32_t cluster, bool recursive) const
{
  size_t entries = cluster ? dir_entries_per_cluster : bios.num_root_entries;

  std::vector<FAT_entry *> dirs;

  for(FAT_entry &e : FAT_entry_iterator(*this, cluster, entries))
  {
    if(!e.exists())
      continue;

    char filename[13];
    e.name(filename, sizeof(filename));

    FileInfo tmp(base, filename, e.fileinfo_type(), e.size());

    tmp.access(FileInfo::convert_DOS(e.access_date(), 0));
    tmp.create(FileInfo::convert_DOS(e.create_date(), e.create_time()));
    tmp.modify(FileInfo::convert_DOS(e.modify_date(), e.modify_time()));
    tmp.priv = &e;

    if(tmp.filter(filter, filter_flags))
      dest.push_back(std::move(tmp));

    if(recursive && (e.data[0] != '.') && (e.attributes() & FAT_entry::DIRECTORY))
      dirs.push_back(&e);
  }

  if(recursive)
  {
    for(FAT_entry *e : dirs)
    {
      char path[1024];
      size_t len = 0;
      if(base && base[0])
      {
        len = snprintf(path, sizeof(path), "%s\\", base);
        if(len >= sizeof(path))
          continue;
      }

      e->name(path + len, sizeof(path) - len);
      search_r(dest, filter, filter_flags, path, e->cluster(), recursive); // TODO: cluster32
    }
  }
}

bool FAT_image::Test(const FileInfo &file)
{
  // FIXME
  return true;
}

bool FAT_image::Extract(const FileInfo &file, const char *destdir)
{
  // FIXME
  return false;
}


class FAT12_image: public FAT_image
{
public:
  FAT12_image(const char *_type, const char *_media, const FAT_bios &_bios, FILE *fp):
   FAT_image::FAT_image(_type, _media, _bios)
  {
    size_t fat_size = bios.bytes_per_sector * bios.num_sectors_per_fat;

    size = bios.bytes_per_sector * (bios.num_sectors + bios.num_hidden_sectors);
    fat_entries = fat_size * 2 / 3;
    if(fat_entries < 3)
    {
      error_state = true;
      return;
    }

    FAT_image::init_FAT();

    // Skip reserved sectors.
    size_t reserved_size = (size_t)bios.reserved_sectors * bios.bytes_per_sector;
    fseek(fp, reserved_size, SEEK_SET);

    /* Load FAT(s). */
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[fat_size]);

    for(size_t i = 0; i < bios.num_fats; i++)
    {
      uint32_t *entries = fat[i];
      uint8_t *pos = buffer.get();

      if(!fread(pos, fat_size, 1, fp))
      {
        error_state = true;
        return;
      }

      for(size_t j = 0; (j + 1) < fat_entries; j += 2)
      {
        *(entries++) = ((pos[1] & 0xf) << 8) | pos[0];
        *(entries++) = (pos[1] >> 4) | (pos[2] << 4);

        pos += 3;
      }
    }

    end_of_chain = fat[0][1];

    /* Load root and data area.
     * Unlike FAT32, the root is not technically part of the data area,
     * so clusters need to be indexed further in this buffer. */
    root_size = bios.num_root_entries * 32;
    root_sectors = root_size / bios.bytes_per_sector;
    if(root_size & (bios.bytes_per_sector - 1))
    {
      error_state = true;
      return;
    }

    data_area_size = size - reserved_size - (bios.num_fats * fat_size);
    data_area = new uint8_t[data_area_size];
    if(!fread(data_area, data_area_size, 1, fp))
      error_state = true;
  }
};



class AtariST_image: public FAT12_image
{
public:
  AtariST_image(const FAT_bios &_bios, FILE *fp): FAT12_image::FAT12_image("Atari ST", "3.5\"", _bios, fp) {}
};


/**
 * FAT loaders.
 */
class AtariSTLoader: public DiskImageLoader
{
public:
  virtual DiskImage *Load(FILE *fp, long file_length) const override
  {
    AtariST_FAT12_boot d{};
    uint8_t boot_sector[512];

    if(!fread(boot_sector, sizeof(boot_sector), 1, fp))
      return nullptr;
    uint16_t checksum = 0;

    for(size_t i = 0; i < sizeof(boot_sector); i += 2)
      checksum += mem_u16be(boot_sector + i);

    if(checksum != 0x1234)
      return nullptr;

    d.jump = mem_u16le(boot_sector + 0);
    memcpy(d.oem, boot_sector + 2, sizeof(d.oem));
    memcpy(d.serial, boot_sector + 8, sizeof(d.serial));
    bios_3_0(d.bios, boot_sector);

    memcpy(d.priv, boot_sector + 30, sizeof(d.priv));

    d.checksum = mem_u16le(boot_sector + 510);


    AtariST_image *disk = new AtariST_image(d.bios, fp);

    /**
     * Several cases seem common:
     * 1) OEM is all printable characters like it's supposed to be.
     * 2) The first byte is 0x90 (NOP?) or 0x00, the rest are printable, e.g. 0x90 IBM 0x20 0x20.
     * 3) The string is nonprintable garbage.
     */
    size_t len = 0;
    int printable = 0;
    for(size_t i = 0; i < sizeof(d.oem); i++)
      if(isprint(d.oem[i]))
        printable++;

    if(printable == 6)
    {
      len = snprintf(disk->oem, sizeof(disk->oem), "`%6.6s`", (char *)d.oem);
    }
    else

    if(printable == 5 && !isprint(d.oem[0]))
    {
      len = snprintf(disk->oem, sizeof(disk->oem), "%02Xh `%5.5s`", d.oem[0], (char *)d.oem + 1);
    }
    else
    {
      len = snprintf(disk->oem, sizeof(disk->oem), "%02Xh %02Xh %02Xh %02Xh %02Xh %02Xh",
       d.oem[0], d.oem[1], d.oem[2], d.oem[3], d.oem[4], d.oem[5]);
    }

    snprintf(disk->oem + len, sizeof(disk->oem) - len, " : %02Xh %02Xh %02Xh",
     d.serial[0], d.serial[1], d.serial[2]);

    return disk;
  }
};

static const AtariSTLoader loader;
