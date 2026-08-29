/*
 * Minimal in-memory TIFFClientOpen backend shared by the reframer and the decoder.
 * libtiff has no built-in "open from buffer" API, only file/client-handle based I/O.
 */

#ifndef TIFF_MEM_IO_H
#define TIFF_MEM_IO_H

#include <string.h>
#include <tiff/tiffio.h>

typedef struct
{
	const unsigned char *data;
	tmsize_t size;
	toff_t offset;
} TiffMemHandle;

static tmsize_t tiffmem_read(thandle_t h, void *buf, tmsize_t n)
{
	TiffMemHandle *m = (TiffMemHandle *)h;
	tmsize_t avail = m->size - (tmsize_t)m->offset;
	if (avail < 0)
		avail = 0;
	if (n > avail)
		n = avail;
	memcpy(buf, m->data + m->offset, (size_t)n);
	m->offset += n;
	return n;
}

static tmsize_t tiffmem_write(thandle_t h, void *buf, tmsize_t n)
{
	/* read-only backend */
	return -1;
}

static toff_t tiffmem_seek(thandle_t h, toff_t off, int whence)
{
	TiffMemHandle *m = (TiffMemHandle *)h;
	toff_t new_off;
	switch (whence)
	{
	case SEEK_SET:
		new_off = off;
		break;
	case SEEK_CUR:
		new_off = (toff_t)m->offset + off;
		break;
	case SEEK_END:
		new_off = (toff_t)m->size + off;
		break;
	default:
		return (toff_t)-1;
	}
	m->offset = new_off;
	return new_off;
}

static int tiffmem_close(thandle_t h)
{
	return 0;
}

static toff_t tiffmem_size(thandle_t h)
{
	TiffMemHandle *m = (TiffMemHandle *)h;
	return (toff_t)m->size;
}

static int tiffmem_map(thandle_t h, void **base, toff_t *size)
{
	TiffMemHandle *m = (TiffMemHandle *)h;
	*base = (void *)m->data;
	*size = (toff_t)m->size;
	return 1;
}

static void tiffmem_unmap(thandle_t h, void *base, toff_t size)
{
}

static TIFF *tiffmem_open(TiffMemHandle *handle, const unsigned char *data, unsigned int size)
{
	handle->data = data;
	handle->size = (tmsize_t)size;
	handle->offset = 0;
	return TIFFClientOpen("mem", "rm", (thandle_t)handle,
						  tiffmem_read, tiffmem_write, tiffmem_seek, tiffmem_close,
						  tiffmem_size, tiffmem_map, tiffmem_unmap);
}

#endif /* TIFF_MEM_IO_H */
