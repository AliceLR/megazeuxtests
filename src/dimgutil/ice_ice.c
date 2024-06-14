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
 const uint8_t *data, size_t size)
{
	struct mem m = { data, size, 0 };
	void *out = NULL;
	long out_size;

	out_size = ice1_unpack_test(data, size);
	if (out_size >= 0 && out_size <= ICE_DEPACK_LIMIT) {
		out = malloc(out_size);
		if (!out)
			return -1;
		if (ice1_unpack(out, out_size, mem_read, mem_seek, &m, size) < 0)
			goto err;

		*_out = out;
		*_out_size = out_size;
		return 0;
	}

	out_size = ice2_unpack_test(data, size);
	if (out_size >= 0 && out_size <= ICE_DEPACK_LIMIT) {
		out = malloc(out_size);
		if (!out)
			return -1;
	for (size_t i = 0; i < 1000000; i++)
		if (ice2_unpack(out, out_size, mem_read, mem_seek, &m, size) < 0)
			goto err;

		*_out = out;
		*_out_size = out_size;
		return 0;
	}

err:
	free(out);
	return -1;
}


#ifdef LIBFUZZER_FRONTEND
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	void *out = NULL;
	size_t out_size;
	if (test_and_depack(&out, &out_size, data, size) == 0)
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
	int ret;

	if(argc < 2)
		return -1;

#ifdef _WIN32
	/* Windows forces stdout to be text mode by default, fix it. */
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	f = fopen(argv[1], "rb");
	if(!f)
		return -1;

	fseek(f, 0, SEEK_END);
	file_length = ftell(f);
	rewind(f);
	if ((data = malloc(file_length)) == NULL) {
		fclose(f);
		return -1;
	}
	if (fread(data, 1, file_length, f) < (size_t)file_length) {
		fclose(f);
		return -1;
	}
	fclose(f);

	ret = test_and_depack(&out, &out_size, (uint8_t *)data, file_length);
	free(data);

	if (ret < 0)
		return ret;

	fwrite(out, out_size, 1, stdout);
	free(out);
	return 0;
}
