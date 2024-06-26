/**
 * modunpack: disk image and archive utility
 * Copyright (C) 2024 Alice Rowan <petrifiedrowan@gmail.com>
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

#include "ice_unpack.h"

#ifdef _WIN32
#include <fcntl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* So the fuzzer doesn't crash trying to allocate 4GiB. */
#define ICE_DEPACK_LIMIT	(1 << 28)

#ifdef LIBFUZZER_FRONTEND
#define ICE_OUTPUT(...)
#else
#define ICE_OUTPUT(...) do{ fprintf(stderr, ""  __VA_ARGS__); fflush(stderr); }while(0)
#endif

struct mem
{
	const uint8_t *data;
	size_t size;
	size_t pos;
};

size_t mem_read(void * ICE_RESTRICT dest, size_t num, void *priv)
{
	struct mem *m = (struct mem *)priv;
	num = ICE_MIN(num, m->size - m->pos);

	memcpy(dest, m->data + m->pos, num);
	m->pos += num;
	return num;
}

long mem_seek(void *priv, long offset, int whence)
{
	struct mem *m = (struct mem *)priv;
	switch (whence) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		offset += (long)m->pos;
		break;
	case SEEK_END:
		offset += (long)m->size;
		break;
	default:
		return -1; /* EINVAL */
	}
	if (offset < 0 || (size_t)offset > m->size)
		return -1; /* EINVAL */

	m->pos = (size_t)offset;
	return 0;
}

__attribute__((noinline))
int test_and_depack(void **_out, size_t *_out_size,
 const uint8_t *data, size_t size, size_t repeat_times)
{
	struct mem m = { data, size, 0 };
	void *out = NULL;
	long out_size;
	size_t i;

	out_size = ice1_unpack_test(data, size);
	if (out_size >= 0) {
		ICE_OUTPUT("format: Pack-Ice v1\n");
		if (out_size == 0 || out_size > ICE_DEPACK_LIMIT) {
			ICE_OUTPUT("unsupported output size %ld\n", out_size);
			return -1;
		}
		out = malloc(out_size);
		if (!out) {
			ICE_OUTPUT("alloc error\n");
			return -1;
		}
		for (i = 0; i < repeat_times; i++) {
			if (ice1_unpack(out, out_size, mem_read, mem_seek, &m, size) < 0) {
				ICE_OUTPUT("unpack error\n");
				goto err;
			}
		}

		*_out = out;
		*_out_size = out_size;
		return 0;
	}

	out_size = ice2_unpack_test(data, size);
	if (out_size >= 0) {
		ICE_OUTPUT("format: Pack-Ice v2\n");
		if (out_size == 0 || out_size > ICE_DEPACK_LIMIT) {
			ICE_OUTPUT("unsupported output size %ld\n", out_size);
			return -1;
		}
		out = malloc(out_size);
		if (!out) {
			ICE_OUTPUT("alloc error\n");
			return -1;
		}
		for (i = 0; i < repeat_times; i++) {
			if (ice2_unpack(out, out_size, mem_read, mem_seek, &m, size) < 0) {
				ICE_OUTPUT("unpack error\n");
				goto err;
			}
		}

		*_out = out;
		*_out_size = out_size;
		return 0;
	}

	ICE_OUTPUT("not a Pack-Ice file\n");
err:
	free(out);
	return -1;
}


#ifdef LIBFUZZER_FRONTEND
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	void *out = NULL;
	size_t out_size;
	if (test_and_depack(&out, &out_size, data, size, 1) == 0)
		free(out);
	return 0;
}

#define main _main
static __attribute__((unused))
#endif

int main(int argc, char *argv[])
{
	FILE *f;
	void *out;
	size_t out_size;
	void *data;
	unsigned long file_length;
	size_t repeats = 1;
	int ret;

	if(argc < 2) {
		ICE_OUTPUT("usage: %s filename.ext >output\n", argv[0]);
		return -1;
	}

#ifdef _WIN32
	/* Windows forces stdout to be text mode by default, fix it. */
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	if (argc >= 3) {
		/* Repeat depacking multiple times without writing output.
		 * Only useful for rough performance comparisons. */
		size_t val = strtoul(argv[2], NULL, 10);
		if (val) {
			ICE_OUTPUT("UNPACKING %zu TIMES: NO DATA WILL BE OUTPUT\n", val);
			repeats = val;
		}
	}

	f = fopen(argv[1], "rb");
	if(!f) {
		ICE_OUTPUT("failed to open file '%s'\n", argv[1]);
		return -1;
	}

	fseek(f, 0, SEEK_END);
	file_length = ftell(f);
	rewind(f);
	if ((data = malloc(file_length)) == NULL) {
		ICE_OUTPUT("alloc error on input\n");
		fclose(f);
		return -1;
	}
	if (fread(data, 1, file_length, f) < (size_t)file_length) {
		ICE_OUTPUT("read error on input\n");
		fclose(f);
		return -1;
	}
	fclose(f);

	ret = test_and_depack(&out, &out_size, (uint8_t *)data, file_length, repeats);
	free(data);

	if (ret < 0 || repeats > 1) {
		free(out);
		return ret;
	}

	fwrite(out, out_size, 1, stdout);
	free(out);
	return 0;
}
