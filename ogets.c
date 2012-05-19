#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <png.h>

#define buf2u32(b) ((b)[0] << 24) + ((b)[1] << 16) + ((b)[2] << 8) + (b)[3]
#define u322buf(u,b) do { \
	(b)[0] = ((u) >> 24) & 0xff; \
	(b)[1] = ((u) >> 16) & 0xff; \
	(b)[2] = ((u) >>  8) & 0xff; \
	(b)[3] =  (u)        & 0xff; \
} while (0)

static uint8_t *fixed_buffer(uint32_t len)
{
	uint8_t *buf;
	buf = calloc(len + 4 + 4, sizeof(uint8_t));
	if (buf) {
		u322buf(len, buf);
	}
	return buf;
}

static inline uint8_t hide_byte(uint8_t dst, uint8_t bit)
{
	return (dst & 0xfe) | (bit & 0x01);
}

static int hide(png_bytep *raw, int w, int h, uint8_t *buf, size_t len)
{
	if (len == 0) { return 0; }

	uint8_t c = buf[0]; /* shift register */

	int x, y; /* [x][y] coords of pixel */

	uint8_t bits = 8; /* num bits left in c */
	size_t i = 1; /* global iter into buf */
	for (x = 0; x < w && i < len; x++) {
		for (y = 0; y < h && i < len; y++) {
			bits--;
			raw[x][y] = hide_byte(raw[x][y], c >> bits);
			if (bits == 0) {
				c = buf[i++];
				bits = 8;
			}
		}
	}

	return 0;
}

static int retrieve(png_bytep *raw, int w, int h, uint8_t **buf, size_t *len)
{
	if (!buf || !len) { return 1; }

	uint8_t *trim = NULL;
	uint8_t c = 0;
	int x, y;

	size_t i = 0;
	uint8_t bits = 8;
	for (x = 0; x < w; x++) {
		for (y = 0; y < h; y++) {
			bits--;
			c = (c << 1) + (raw[x][y] & 0x01);
			if (bits == 0) {
				(*buf)[i++] = c;
				c = 0;
				bits = 8;

				if (i == 4) {
					*len = buf2u32(*buf);
				} else if (i > 4 && i+4 >= *len) {
					goto finished;
				}
			}
		}
	}

finished:

	trim = calloc(*len+1, sizeof(uint8_t));
	memcpy(trim, *buf+4, *len-1);
	free(*buf);
	*buf = trim;

	return 0;
}

int hide_in(const char *in_file, const char *out_file, uint8_t *buf, size_t len)
{
	FILE *io = fopen(in_file, "rb");
	if (!io) {
		perror(in_file);
		return 2;
	}

	png_structp PNG;
	png_infop INFO;

	PNG = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!PNG) {
		fprintf(stderr, "png_create_read_struct failed\n");
		return 1;
	}
	png_init_io(PNG, io);

	INFO = png_create_info_struct(PNG);
	if (!INFO) {
		fprintf(stderr, "png_create_info_struct failed\n");
		return 1;
	}

	png_read_png(PNG, INFO, PNG_TRANSFORM_IDENTITY, NULL);

	int width, height;
	width  = png_get_image_width(PNG, INFO);
	height = png_get_image_height(PNG, INFO);

	png_bytep *raw = png_get_rows(PNG, INFO);
	fclose(io);

	int size = width * height;
	int capacity = size / 8 - 4;
	printf("%0.1f kb storable in %0.1f kb of image data\n", capacity / 1024.0, size / 1024.0);

	hide(raw, width, height, buf, len);

	io = fopen(out_file, "wb");
	PNG = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!PNG) {
		fprintf(stderr, "png_create_write_struct failed\n");
		return 1;
	}
	png_init_io(PNG, io);
	png_set_rows(PNG, INFO, raw);

	png_write_png(PNG, INFO, PNG_TRANSFORM_IDENTITY, NULL);
	fclose(io);

	return 0;
}

int read_from(const char *png_file, uint8_t **buf, size_t *len)
{
	if (!buf || !len) {
		return -1;
	}

	FILE *io = fopen(png_file, "rb");
	if (!io) {
		perror(png_file);
		return 2;
	}

	png_structp PNG;
	png_infop INFO;

	PNG = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!PNG) {
		fprintf(stderr, "png_create_read_struct failed\n");
		return 1;
	}
	png_init_io(PNG, io);

	INFO = png_create_info_struct(PNG);
	if (!INFO) {
		fprintf(stderr, "png_create_info_struct failed\n");
		return 1;
	}

	png_read_png(PNG, INFO, PNG_TRANSFORM_IDENTITY, NULL);

	int width, height;
	width  = png_get_image_width(PNG, INFO);
	height = png_get_image_height(PNG, INFO);

	int size = width * height;
	int capacity = size / 8 - 4;
	printf("%0.1f kb storable in %0.1f kb of image data\n", capacity / 1024.0, size / 1024.0);

	png_bytep *raw = png_get_rows(PNG, INFO);
	fclose(io);

	*buf = calloc(capacity, sizeof(uint8_t));
	if (!*buf) {
		perror("calloc");
		return 1;
	}
	retrieve(raw, width, height, buf, len);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "USAGE: ogets (hide|show) <file>\n");
		return 1;
	}

	uint8_t *tmp, *buf;
	uint32_t len;

	if (strcmp(argv[1], "hide") == 0) {
		/* read data; store it in png pixel data */
		tmp = calloc(8192, sizeof(uint8_t));
		len = read(0, tmp, 8192);
		if (len < 0) {
			perror("read error");
			exit(1);
		}
		printf("read %u bytes of data to hide\n", len);

		buf = fixed_buffer(len);
		memcpy(buf+4, tmp, len);
		return hide_in(argv[2], "out.png", buf, len+8);

	} else if (strcmp(argv[1], "show") == 0) {
		if (read_from(argv[2], &buf, &len) == 0) {
			printf("Read: '%s'\n", buf);
		}
	}
}
