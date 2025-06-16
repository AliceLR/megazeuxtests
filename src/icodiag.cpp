#include <inttypes.h>
#include <stdio.h>
#include <vector>

#include "common.hpp"
#include "format.hpp"

enum ico_type
{
  ICO_ICON = 1,
  ICO_CURSOR = 2,
};

enum ico_error
{
  ICO_OK,
  ICO_READ_ERROR,
  ICO_SEEK_ERROR,
  ICO_NOT_AN_ICO,
};

static constexpr const char *ico_strerror(ico_error value)
{
  switch(value)
  {
    case ICO_OK:         return "ok";
    case ICO_READ_ERROR: return "read error";
    case ICO_SEEK_ERROR: return "seek error";
    case ICO_NOT_AN_ICO: return "not an .ICO file";
  }
  return "unknown error";
}

struct ico_dirent
{
  uint8_t  width_px;
  uint8_t  height_px;
  uint8_t  palette_size;
  uint8_t  reserved;
  uint16_t field1;
  uint16_t field2;
  uint32_t data_bytes;
  uint32_t data_offset;
};

struct ico
{
  uint16_t reserved;
  uint16_t type;
  uint16_t num_images;
  std::vector<ico_dirent> directory;
};

ico_error ico_read_directory_entry(const ico &ico, ico_dirent &ent,
 const long file_length, FILE *fp)
{
  uint8_t buffer[16];
  long current_pos = ftell(fp);

  if(fread(buffer, 1, 16, fp) < 16)
    return ICO_READ_ERROR;

  ent.width_px     = buffer[0];
  ent.height_px    = buffer[1];
  ent.palette_size = buffer[2];
  ent.reserved     = buffer[3];
  ent.field1       = mem_u16le(buffer + 4);
  ent.field2       = mem_u16le(buffer + 6);
  ent.data_bytes   = mem_u32le(buffer + 8);
  ent.data_offset  = mem_u32le(buffer + 12);

  if(ent.reserved != 0)
  {
    format::warning("  @ %ld: reserved not 0", current_pos + 3);
    return ICO_NOT_AN_ICO;
  }
  if(ico.type == ICO_ICON && ent.field1 > 1)
  {
    format::warning("  @ %ld: bad ICO color planes count %d",
     current_pos + 2, ent.field1);
    return ICO_NOT_AN_ICO;
  }

  long size = static_cast<long>(ent.data_bytes);
  long offset = static_cast<long>(ent.data_offset);
  if(size < 0 || size > file_length)
  {
    format::warning("  @ %ld: bad image size: %ld / %" PRIx32 "h",
     current_pos + 8, size, ent.data_bytes);
    return ICO_NOT_AN_ICO;
  }
  if(offset < 0 || offset > file_length || file_length - offset < size)
  {
    format::warning("  @ %ld: bad image offset: %ld / %" PRIx32 "h (size: %ld / %" PRIx32 "h)",
     current_pos + 12, offset, ent.data_offset, size, ent.data_bytes);
    return ICO_NOT_AN_ICO;
  }
  return ICO_OK;
}

ico_error ico_test_file(FILE *fp)
{
  ico ico{};
  long file_length;
  uint8_t buffer[6];
  size_t i;

  file_length = get_file_length(fp);
  if(file_length < 0)
  {
    format::warning("  could not query length");
    return ICO_SEEK_ERROR;
  }
  if(fread(buffer, 1, 6, fp) < 6)
  {
    format::warning("  @ 0");
    return ICO_READ_ERROR;
  }

  ico.reserved   = mem_u16le(buffer);
  ico.type       = mem_u16le(buffer + 2);
  ico.num_images = mem_u16le(buffer + 4);

  if(ico.reserved != 0)
  {
    format::warning("  @ 0: reserved field is not 0");
    return ICO_NOT_AN_ICO;
  }
  if(ico.type != ICO_ICON && ico.type != ICO_CURSOR)
  {
    format::warning("  @ 2: type isn't ICO or CUR");
    return ICO_NOT_AN_ICO;
  }

  /* First pass: test only. */
  for(i = 0; i < ico.num_images; i++)
  {
    ico_dirent ent;
    ico_error ret = ico_read_directory_entry(ico, ent, file_length, fp);
    if(ret)
    {
      format::warning("  directory entry %zu", i);
      return ret;
    }
  }

  /* Load */
  ico.directory.reserve(ico.num_images);
  if(fseek(fp, 6, SEEK_SET) < 0)
  {
    format::warning("  failed seek to start");
    return ICO_SEEK_ERROR;
  }
  for(i = 0; i < ico.num_images; i++)
  {
    ico_dirent &ent = ico.directory[i];
    ico_error ret = ico_read_directory_entry(ico, ent, file_length, fp);
    if(ret)
    {
      format::warning("  directory entry %zu (load)", i);
      return ret;
    }
  }

  /* Print info */
  format::line("Type",   ico.type == ICO_ICON ? "ICO" : "CUR");
  format::line("Images", "%d", ico.num_images);

  namespace table = format::table;
  static const char *labels[] =
  {
    "Width", "Height", "Colors", "D1", "D2", "Offset", "Size"
  };

  format::line();
  table::table<
    table::number<6>,
    table::number<6>,
    table::number<6>,
    table::spacer,
    table::number<5>,
    table::number<5>,
    table::spacer,
    table::number<10>,
    table::number<10>> tbl;

  tbl.header("Images", labels);
  for(i = 0; i < ico.num_images; i++)
  {
    ico_dirent &ent = ico.directory[i];
    tbl.row(i + 1,
      ent.width_px ? ent.width_px : 256,
      ent.height_px ? ent.height_px : 256,
      ent.palette_size,
      {},
      ent.field1,
      ent.field2,
      {},
      ent.data_offset,
      ent.data_bytes);
  }
  format::endline();
  return ICO_OK;
}

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
    ico_test_file(fp);
    fclose(fp);
  }
  return 0;
}

#define main _main
static __attribute__((unused))
#endif

int main(int argc, char *argv[])
{
  for(int i = 1; i < argc; i++)
  {
    FILE *fp = fopen(argv[i], "rb");
    if(fp)
    {
      ico_error ret = ico_test_file(fp);
      if(ret != ICO_OK)
        format::error("file '%s': %s", argv[i], ico_strerror(ret));

      fclose(fp);
    }
    else
      format::error("file '%s' does not exist or permission denied", argv[i]);
  }
  return 0;
}
