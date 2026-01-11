/**
 * Copyright (C) 2025 Lachesis <petrifiedrowan@gmail.com>
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

/* CoconizerSong Relocatable Module player with embedded Coconizer module. */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define error(...) do { \
  fprintf(stderr, "" __VA_ARGS__); \
  fflush(stderr); \
} while(0)

struct relocatable_module_header
{
  uint32_t start_address;
  uint32_t init_address;
  uint32_t finish_address;
  uint32_t service_handler;
  uint32_t title_address;
  uint32_t help_address;
  uint32_t keywords_address;

  uint8_t coconizersong[16];
};

struct coconizer_instrument
{
  uint32_t offset;
  uint32_t length;
  uint32_t volume;
  uint32_t loop_start;
  uint32_t loop_length;
  char name[11];
  uint8_t unused;
};

struct coconizer_header
{
  uint8_t num_channels;
  char title[20];
  uint8_t num_instruments;
  uint8_t num_orders;
  uint8_t num_patterns;
  uint32_t orders_offset;
  uint32_t patterns_offset;

  struct coconizer_instrument instruments[100];
};

static uint32_t mem_u32le(const uint8_t *b)
{
  return b[0] | (b[1] << 8u) | (b[2] << 16u) | (b[3] << 24u);
}

static int get_relocatable_module_header(struct relocatable_module_header *h, FILE *f)
{
  uint8_t buf[44];

  if (fread(buf, 1, 44, f) < 44)
  {
    error("failed to read relocatable module header\n");
    return -1;
  }

  /* Acorn 26-bit relocatable module format. */
  h->start_address    = mem_u32le(buf + 0);
  h->init_address     = mem_u32le(buf + 4);
  h->finish_address   = mem_u32le(buf + 8);
  h->service_handler  = mem_u32le(buf + 12);
  h->title_address    = mem_u32le(buf + 16);
  h->help_address     = mem_u32le(buf + 20);
  h->keywords_address = mem_u32le(buf + 24);

  memcpy(h->coconizersong, buf + 28, 16);
  return 0;
}

static int check_relocatable_module_header(struct relocatable_module_header *h)
{
  if (h->start_address != 0)
  {
    error("not CoconizerSong: bad start address %08" PRIx32 "h\n",
      h->start_address);
    return -1;
  }

  if ((h->init_address & 3) || h->init_address < 0x2c || h->init_address >= 0x400)
  {
    error("not CoconizerSong: bad init address %08" PRIx32 "h\n",
      h->init_address);
    return -1;
  }

  if ((h->finish_address & 3) || h->finish_address < 0x2c ||
      h->finish_address >= 0x400 || h->finish_address < h->init_address)
  {
    error("not CoconizerSong: bad finish address %08" PRIx32 "h\n",
      h->finish_address);
    return -1;
  }

  if (h->service_handler != 0)
  {
    error("not CoconizerSong: bad service handler address %08" PRIx32 "h\n",
      h->service_handler);
    return -1;
  }

  if (h->title_address != 0x1c)
  {
    error("not CoconizerSong: bad title address %08" PRIx32 "h\n",
      h->title_address);
    return -1;
  }

  if ((h->help_address & 3) || (h->help_address && h->help_address < 0x2c) ||
      h->help_address >= 0x400)
  {
    error("not CoconizerSong: bad help address %08" PRIx32 "h\n",
      h->help_address);
    return -1;
  }

  if ((h->keywords_address & 3) ||
      (h->keywords_address && h->keywords_address < 0x2c) ||
      h->keywords_address >= 0x400 ||
      (h->help_address && h->keywords_address &&
       h->help_address > h->keywords_address))
  {
    error("not CoconizerSong: bad keywords address %08" PRIx32 "h\n",
      h->keywords_address);
    return -1;
  }

  if (memcmp(h->coconizersong, "CoconizerSong\0\0", 16))
  {
    error("not CoconizerSong: title string isn't 'CoconizerSong\\0\\0\\0'\n");
    return -1;
  }
  return 0;
}

/* CoconizerSong executables don't contain a convenient module address.
 * They use two instances of ADR (10,Track) to source the track address.
 * ADR will emit either ADD or SUB instructions; in this case, it should
 * almost always be two ADD instructions.
 *
 * From finish address, load 1024 and scan for the instruction:
 *   31[cond]28 27[00]26 [immediate if 1]25 24[opcode]21 [status]20
 *   19[Rn]16 15[Rd]12 11[operand2]0
 *
 * [1110=always][00][1][0100=ADD][0]
 * [Rn=PC=1111][Rd=R10=1010][PC-relative offset]
 * xx Ax 8F E2
 *
 * Example: Computer Festival 1 by Neil Coffey
 * Module is at 0xb98. This particular module has two usable instances:
 *
 * PC = 0x2c4 (pipelining)
 * 2bc: e28fab02	-> ADD R10, PC, (2 << 10)
 * 2c0: e28aa0d4	-> ADD R10, R10, 0x0d4
 * 			-> R10 = 0xB98
 *
 * PC = 0x300 (pipelining)
 * 2f8: e28fab02	-> ADD R10, PC, (2 << 10)
 * 2fc: e28aa098	-> ADD R10, R10, 0x098
 * 			-> R10 = 0xB98
 */
#define ADR_INSTR(x)		((x) & 0xfffff000ul)
#define ADR_ADD_R10_PC		0xe28fa000ul
#define ADR_ADD_R10_R10		0xe28aa000ul
#define ADR_IMM_SHIFT(x)	(((x) & 0xf00u) >> 7u)
#define ADR_IMM_BASE(x)		((x) & 0xffu)

static uint32_t get_arm_instruction_immediate(uint32_t instruction)
{
  uint32_t value = ADR_IMM_BASE(instruction);
  uint32_t r = ADR_IMM_SHIFT(instruction);
  uint32_t l = 32u - r;

  return (r == 0) ? value : (value >> r) | (value << l);
}

static int get_coconizer_start_offset(struct relocatable_module_header *h, FILE *f)
{
  uint8_t buf[1024];
  uint8_t *pos;
  uint8_t *end;
  uint32_t pc;
  uint8_t x;

  if (fseek(f, (long)h->finish_address, SEEK_SET) < 0)
    return -1;
  if (fread(buf, 1, sizeof(buf), f) < sizeof(buf))
    return -1;

  pos = buf;
  end = pos + sizeof(buf);
  pc = h->finish_address;
  while (pos < end)
  {
    uint32_t instruction = mem_u32le(pos);
    uint32_t offset;
    pos += 4;
    pc += 4;
    if (ADR_INSTR(instruction) != ADR_ADD_R10_PC)
      continue;

    /* PC + 8 (pipelining) - 4 (pre-incremented above) */
    offset = pc + 4;
    offset += get_arm_instruction_immediate(instruction);

    /* Most likely two ADD instructions required, check the next. */
    if (pos < end)
    {
      instruction = mem_u32le(pos);
      pos += 4;
      pc += 4;
      if (ADR_INSTR(instruction) == ADR_ADD_R10_R10)
        offset += get_arm_instruction_immediate(instruction);
    }

    if (fseek(f, (long)offset, SEEK_SET) < 0)
      continue;

    /* Offset should contain the initial channel
     * count byte without the module flag set. */
    x = fgetc(f);
    if (x != 0x04 && x != 0x08)
      continue;

    error("located Coconizer module at %08x\n", offset);
    return offset;
  }
  error("failed to locate Coconizer module\n");
  return -1;
}

static int check_cr(const char *name, size_t name_len)
{
  size_t i;
  for (i = 0; i < name_len; i++)
    if (name[i] == '\r')
      return 0;

  return -1;
}

static int get_coconizer_module_header(struct coconizer_header *coco,
                                       int start_offset, FILE *f)
{
  uint8_t buffer[32];
  unsigned i;

  if (fseek(f, start_offset, SEEK_SET) < 0)
  {
    error("seek error loading Coconizer module header\n");
    return -1;
  }
  if (fread(buffer, 1, 32, f) < 32)
  {
    error("read error loading Coconizer module header\n");
    return -1;
  }
  memcpy(coco->title, buffer + 1, sizeof(coco->title));
  coco->num_channels    = buffer[0] & 0x3f;
  coco->num_instruments = buffer[21];
  coco->num_orders      = buffer[22];
  coco->num_patterns    = buffer[23];
  coco->orders_offset   = mem_u32le(buffer + 24);
  coco->patterns_offset = mem_u32le(buffer + 28);

  /* Safety checks copied from libxmp. */

  if (coco->num_channels != 0x04 && coco->num_channels != 0x08)
  {
    error("not Coconizer: bad channel count %d\n", coco->num_channels);
    return -1;
  }
  if (check_cr(coco->title, 20) < 0)
  {
    error("not Coconizer: title doesn't contain \\r\n");
    return -1;
  }
  if (coco->num_instruments == 0 || coco->num_instruments > 100)
  {
    error("not Coconizer: bad instrument count %d\n", coco->num_instruments);
    return -1;
  }
  if (coco->orders_offset < 64 || coco->orders_offset > 0x00100000)
  {
    error("not Coconizer: bad orders offset %08" PRIx32 "h\n",
      coco->orders_offset);
    return -1;
  }
  if (coco->patterns_offset < 64 || coco->patterns_offset > 0x00100000)
  {
    error("not Coconizer: bad patterns offset %08" PRIx32 "h\n",
      coco->patterns_offset);
    return -1;
  }

  /* Instruments */
  for (i = 0; i < coco->num_instruments; i++)
  {
    struct coconizer_instrument *ins = &coco->instruments[i];

    if (fread(buffer, 1, 32, f) < 32)
    {
      error("read error loading Coconizer instrument %u\n", i);
      return -1;
    }

    ins->offset       = mem_u32le(buffer + 0);
    ins->length       = mem_u32le(buffer + 4);
    ins->volume       = mem_u32le(buffer + 8);
    ins->loop_start   = mem_u32le(buffer + 12);
    ins->loop_length  = mem_u32le(buffer + 16);
    ins->unused       = buffer[31];
    memcpy(ins->name, buffer + 20, 11);

    if (ins->offset < 64 || ins->offset > 0x00100000)
    {
      error("not Coconizer: instrument %u bad offset %08" PRIx32 "h\n",
        i, ins->offset);
      return -1;
    }
    if (ins->volume > 0xff)
    {
      error("not Coconizer: instrument %u bad volume %08" PRIx32 "h\n",
        i, ins->volume);
      return -1;
    }
    if (ins->length > 0x00100000)
    {
      error("not Coconizer: instrument %u bad length %08" PRIx32 "h\n",
        i, ins->length);
      return -1;
    }
    if (ins->loop_start > 0x00100000)
    {
      error("not Coconizer: instrument %u bad loop start %08" PRIx32 "h\n",
        i, ins->loop_start);
      return -1;
    }
    if (ins->loop_length > 0x00100000)
    {
      error("not Coconizer: instrument %u bad loop length %08" PRIx32 "h\n",
        i, ins->loop_length);
      return -1;
    }
    if (ins->loop_start > 0 &&
        ins->loop_start + ins->loop_length - 1 > ins->length)
    {
      error("not Coconizer: instrument %u bad loop: "
        "length:%08" PRIx32 "h, lstart:%08" PRIx32 "h, llength%08" PRIx32 "h\n",
        i, ins->length, ins->loop_start, ins->loop_length);
      return -1;
    }
  }
  return 0;
}

static size_t get_coconizer_module_length(struct coconizer_header *coco, FILE *f)
{
  unsigned pattern_length = coco->num_channels * 64 * 4;

  size_t end_of_header;
  size_t end_of_patterns;
  size_t end_of_orders;
  size_t end_of_sample;
  size_t max_end;
  unsigned i;

  /* This is the absolute lowest position the module should end. */
  end_of_header = 32 + 32 * coco->num_instruments;
  max_end = end_of_header;

  /* End of pattern data */
  end_of_patterns = coco->patterns_offset;
  end_of_patterns += coco->num_patterns * pattern_length;
  if (end_of_patterns > max_end)
    max_end = end_of_patterns;

  /* End of sequence data */
  end_of_orders = coco->orders_offset + coco->num_orders;
  if (end_of_orders > max_end)
    max_end = end_of_orders;

  /* End of samples */
  for (i = 0; i < coco->num_instruments; i++)
  {
    end_of_sample = (size_t)coco->instruments[i].offset +
                            coco->instruments[i].length;

    if (end_of_sample > max_end)
      max_end = end_of_sample;
  }
  error("calculated module length: %zu\n", max_end);
  return max_end;
}

static int copy_module(int start_offset, size_t total_length, FILE *in, FILE *out)
{
  uint8_t buffer[4096];
  size_t num;
  int first_read = 1;

  if (fseek(in, start_offset, SEEK_SET) < 0)
  {
    error("failed to seek to start of Coconizer module\n");
    return -1;
  }

  while (total_length > 0)
  {
    num = (size_t)total_length > sizeof(buffer) ? sizeof(buffer) : total_length;
    total_length -= num;

    if (fread(buffer, 1, num, in) < num)
    {
      error("read error copying module\n");
      return -1;
    }

    if (first_read)
    {
      /* Correct channels byte to have bit 7 (module flag) set. */
      buffer[0] |= 0x80;
      first_read = 0;
    }

    if (fwrite(buffer, 1, num, out) < num)
    {
      error("write error copying module\n");
      return -1;
    }
  }
  error("successfully ripped Coconizer module\n");
  return 0;
}

static int rip_coconizersong(const char *infile, const char *outfile)
{
  struct relocatable_module_header h;
  struct coconizer_header coco;
  size_t total_length;
  int start_offset;
  int ret = -1;
  FILE *out;
  FILE *f;

  f = fopen(infile, "rb");
  if (f == NULL)
  {
    error("failed to open input file: %s\n", infile);
    return -1;
  }

  if (get_relocatable_module_header(&h, f) < 0)
    goto err;
  if (check_relocatable_module_header(&h) < 0)
    goto err;

  start_offset = get_coconizer_start_offset(&h, f);
  if (start_offset < 0)
    goto err;

  if (get_coconizer_module_header(&coco, start_offset, f) < 0)
    goto err;

  total_length = get_coconizer_module_length(&coco, f);

  /* Copy module */
  out = fopen(outfile, "wb");
  if (out != NULL)
  {
    ret = copy_module(start_offset, total_length, f, out);
    fclose(out);
  }

err:
  fclose(f);
  return ret;
}


#ifdef LIBFUZZER_FRONTEND
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  /* TODO: not implemented */
  return -1;
}
#define main _main
static __attribute__((unused))
#endif

int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    error("usage: cocorip [input file] [output file]\n");
    return 0;
  }
  return rip_coconizersong(argv[1], argv[2]);
}
