#include <stdio.h>
#include <vector>

#include "../IFF.hpp"
#include "../common.hpp"

#define AVR_FALSE         0x0000
#define AVR_TRUE          0xffff
#define AVR_NO_MIDI_NOTE  0xffff

struct WAV_file
{
  enum
  {
    FORMAT_PCM = 1,
    FORMAT_IEEE_FLOAT = 3,
  };

  uint16_t format;
  uint16_t format_channels;
  uint32_t sample_rate;
  uint32_t format_bytes_per_sec;
  uint16_t format_bytes_per_frame;
  uint16_t format_bits;
  bool format_signed;

  uint32_t smpl_loop_count;
  uint32_t smpl_loop_start;
  uint32_t smpl_loop_end;

  size_t length_in_bytes;
  size_t length_in_frames;
  std::vector<uint8_t> raw;

  void convert_endian()
  {
    if(format_bits == 16)
    {
      for(size_t i = 0; i < length_in_bytes; i += 2)
      {
        uint8_t tmp = raw[i + 0];
        raw[i + 0] = raw[i + 1];
        raw[i + 1] = tmp;
      }
    }
  }

  void convert_signed()
  {
    if(format_bits == 16)
    {
      uint16_t *data = reinterpret_cast<uint16_t *>(raw.data());
      for(size_t i = 0; i < length_in_bytes; i += 2)
      {
        *(data++) ^= 0x8000;
      }
    }
    else
    {
      uint8_t *data = raw.data();
      for(size_t i = 0; i < length_in_bytes; i++)
        *(data++) ^= 0x80;
    }
    format_signed = !format_signed;
  }
};


class fmt_handler
{
public:
  static constexpr IFFCode id = IFFCode("fmt ");

  static modutil::error parse(FILE *fp, size_t len, WAV_file &wav)
  {
    uint8_t buf[16];
    if(len < sizeof(buf) || fread(buf, 1, sizeof(buf), fp) < sizeof(buf))
    {
      format::error("read error in 'fmt '");
      return modutil::READ_ERROR;
    }

    wav.format                 = mem_u16le(buf + 0);
    wav.format_channels        = mem_u16le(buf + 2);
    wav.sample_rate            = mem_u32le(buf + 4);
    wav.format_bytes_per_sec   = mem_u32le(buf + 8);
    wav.format_bytes_per_frame = mem_u16le(buf + 12);
    wav.format_bits            = mem_u16le(buf + 14);

    wav.format_signed = (wav.format == WAV_file::FORMAT_PCM && wav.format_bits == 8) ? false : true;
    return modutil::SUCCESS;
  }
};

class smpl_handler
{
public:
  static constexpr IFFCode id = IFFCode("smpl");

  static modutil::error parse(FILE *fp, size_t len, WAV_file &wav)
  {
    uint8_t buf[60];
    if(len < sizeof(buf) || fread(buf, 1, sizeof(buf), fp) < sizeof(buf))
    {
      format::error("read error in 'smpl', ignoring");
      return modutil::SUCCESS;
    }
    wav.smpl_loop_count = mem_u32le(buf + 28);
    wav.smpl_loop_start = mem_u32le(buf + 44);
    wav.smpl_loop_end   = mem_u32le(buf + 48) + 1;
    return modutil::SUCCESS;
  }
};

class data_handler
{
public:
  static constexpr IFFCode id = IFFCode("data");

  static modutil::error parse(FILE *fp, size_t len, WAV_file &wav)
  {
    wav.raw.resize(len);
    wav.length_in_bytes = len;
    wav.length_in_frames = len / MIN((uint16_t)1, wav.format_bytes_per_frame);
    if(fread(wav.raw.data(), 1, len, fp) < len)
    {
      format::error("read error in 'data'");
      return modutil::READ_ERROR;
    }
    return modutil::SUCCESS;
  }
};


static const IFF<
  WAV_file,
  fmt_handler,
  smpl_handler,
  data_handler> WAV_parser(Endian::LITTLE, IFFPadding::WORD);


#ifdef LIBFUZZER_FRONTEND
extern "C" {
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	/* TODO: not implemented */
	return -1;
}
}
#define main _main
static __attribute__((unused))
#endif

int main(int argc, char *argv[])
{
  WAV_file wav{};

  if(argc != 3)
  {
    fprintf(stderr, "usage: %s in_file.wav out_file.avr\n", argv[0]);
    return 0;
  }

  FILE *in = fopen(argv[1], "rb");
  if(!in)
  {
    fprintf(stderr, "input file does not exist: %s\n", argv[1]);
    return 1;
  }

  uint8_t buf[12];
  if(fread(buf, 1, 12, in) < 12 ||
   memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4))
  {
    fprintf(stderr, "input file is not a .WAV: %s\n", argv[1]);
    fclose(in);
    return 1;
  }

  size_t in_length = mem_u32le(buf + 4);
  auto parser = WAV_parser;
  modutil::error result = parser.parse_iff(in, in_length, wav);
  fclose(in);
  if(result != modutil::SUCCESS)
  {
    fprintf(stderr, "error loading .WAV: %s\n", modutil::strerror(result));
    return 1;
  }

  if(wav.format != WAV_file::FORMAT_PCM)
  {
    fprintf(stderr, "unsupported format %d\n", wav.format);
    return 1;
  }
  if(wav.format_channels != 1 && wav.format_channels != 2)
  {
    fprintf(stderr, "unsupported channel count %d\n", wav.format_channels);
    return 1;
  }
  if(wav.format_bits != 8 && wav.format_bits != 16)
  {
    fprintf(stderr, "unsupported bits per sample %d\n", wav.format_bits);
    return 1;
  }
  if(wav.sample_rate > 0x00ffffff)
  {
    fprintf(stderr, "unsupported sample rate %" PRIu32 "\n", wav.sample_rate);
    return 1;
  }

  if(wav.smpl_loop_count)
  {
    if(wav.smpl_loop_start > wav.length_in_frames ||
     wav.smpl_loop_end > wav.length_in_frames ||
     wav.smpl_loop_start > wav.smpl_loop_end)
    {
      wav.smpl_loop_count = 0;
      fprintf(stderr, "ignoring invalid loop data %" PRIu32  " %" PRIu32 "\n",
       wav.smpl_loop_start, wav.smpl_loop_end);
    }
  }
  if(!wav.smpl_loop_count)
  {
    // AVR expects this when no loop is present:
    wav.smpl_loop_start = 0;
    wav.smpl_loop_end = wav.length_in_frames;
  }

  // Digital Tracker expects big endian sample data.
  wav.convert_endian();
  // Digital Tracker doesn't know what to do with unsigned samples?
  if(!wav.format_signed && wav.format_bits < 9)
    wav.convert_signed();

  FILE *out = fopen(argv[2], "wb");
  if(!out)
  {
    fprintf(stderr, "output file could not be written: %s\n", argv[2]);
    return 1;
  }

  // Convert filename, if possible.
  char *input_filename = argv[1];
  char *sep = strrchr(input_filename, DIR_SEPARATOR);
  if(sep)
    input_filename = sep + 1;
  size_t name_len = strlen(input_filename);
  char filename[8 + 1]{};
  char filename_ext[20 + 1]{};
  // rare correct usage of this function!
  strncpy(filename, input_filename, 8);
  if(name_len > 8)
    strncpy(filename_ext, input_filename + 8, 20);

  uint8_t zeros[64]{};

  /*   0 */ fputs("2BIT", out);
  /*   4 */ fwrite(filename, 1, 8, out); // Filename
  /*  12 */ fput_u16be(wav.format_channels == 2 ? AVR_TRUE : AVR_FALSE, out); // stereo?
  /*  14 */ fput_u16be(wav.format_bits, out);
  /*  16 */ fput_u16be(wav.format_signed ? AVR_TRUE : AVR_FALSE, out); // signed?
  /*  18 */ fput_u16be(wav.smpl_loop_count ? AVR_TRUE : AVR_FALSE, out); // looping?
  /*  20 */ fput_u16be(AVR_NO_MIDI_NOTE, out); // MIDI note/split
  /*  22 */ fputc(0x03, out); // 24-bit sample rate
            fput_u24be(wav.sample_rate, out);
  /*  26 */ fput_u32be(wav.length_in_bytes, out);
  /*  30 */ fput_u32be(wav.smpl_loop_start, out); // loop start
  /*  34 */ fput_u32be(wav.smpl_loop_end, out); // loop end
  /*  38 */ fput_u16be(0, out); // reserved for MIDI keyboard split
  /*  40 */ fput_u16be(0, out); // reserved for sample compression
  /*  42 */ fput_u16be(0, out); // reserved
  /*  44 */ fwrite(filename_ext, 1, 20, out); // Filename extension
  /*  64 */ fwrite(zeros, 1, 64, out); // User-defined area
  /* 128 */ fwrite(wav.raw.data(), 1, wav.raw.size(), out); // Raw sample data
  fclose(out);
  return 0;
}
