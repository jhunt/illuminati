#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <png.h>

struct ILLU_msg {
	uint32_t  len;
	uint8_t  *data;
};

struct ILLU_png_store {
	uint32_t len;
	uint8_t *data;

	int x, y;

	png_infop   INFO;
	png_bytep  *raw;
	int w, h;

	uint32_t size;
	uint32_t capacity;
};

static struct ILLU_png_store* ILLU_png_store_read(const char *filename)
{
	FILE *io;
	if (strcmp(filename, "-") == 0) {
		io = stdin;
	} else {
		io = fopen(filename, "rb");
	}
	if (!io) {
		perror(filename);
		return NULL;
	}

	png_structp PNG;
	struct ILLU_png_store *store = calloc(1, sizeof(struct ILLU_png_store));
	if (!store) {
		perror("malloc");
		return NULL;
	}

	PNG = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!PNG) {
		fprintf(stderr, "png_create_read_struct failed\n");
		return NULL;
	}
	png_init_io(PNG, io);

	store->INFO = png_create_info_struct(PNG);
	if (!store->INFO) {
		fprintf(stderr, "png_create_info_struct failed\n");
		return NULL;
	}

	png_read_png(PNG, store->INFO, PNG_TRANSFORM_IDENTITY, NULL);
	fclose(io); io = NULL;


	store->x = store->y = 0;
	store->w  = png_get_image_width(PNG, store->INFO);
	store->h = png_get_image_height(PNG, store->INFO);
	store->raw = png_get_rows(PNG, store->INFO);

	store->size = store->w * store->h;
	store->capacity = store->size / 8 - 4 /* len */ - 4 /* "ILLU" */;
	fprintf(stderr, "%0.1f kb storable in %0.1f kb of image data\n", store->capacity / 1024.0, store->size / 1024.0);

	return store;
}

static int ILLU_png_store_write(struct ILLU_png_store *store, const char *filename)
{
	FILE *io;
	if (strcmp(filename, "-") == 0) {
		io = stdout;
	} else {
		io = fopen(filename, "wb");
	}
	if (!io) {
		perror(filename);
		return 1;
	}

	png_structp PNG;

	PNG = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!PNG) {
		fprintf(stderr, "png_create_write_struct failed\n");
		return 1;
	}
	png_init_io(PNG, io);
	png_set_rows(PNG, store->INFO, store->raw);

	png_write_png(PNG, store->INFO, PNG_TRANSFORM_IDENTITY, NULL);
	fclose(io);

	return 0;
}

static uint8_t ILLU_png_store_read_byte(struct ILLU_png_store *store)
{
	size_t i = 8;
	uint8_t byte = 0;

	for (; store->x < store->w; store->x++) {
		for (; store->y < store->h; store->y++) {
			if (i == 0) {
				return byte;
			}

			i--;
			byte = (byte << 1) + (store->raw[store->x][store->y] & 0x01);
		}
		store->y = 0;
	}

	return 0xff;
}

static uint32_t ILLU_png_store_read_u32(struct ILLU_png_store *store)
{
	uint8_t buf[4] = {0};
	buf[0] = ILLU_png_store_read_byte(store);
	buf[1] = ILLU_png_store_read_byte(store);
	buf[2] = ILLU_png_store_read_byte(store);
	buf[3] = ILLU_png_store_read_byte(store);

	return (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
}

static uint8_t ILLU_png_store_write_byte(struct ILLU_png_store* store, uint8_t byte)
{
	size_t i = 8;

	for (; store->x < store->w; store->x++) {
		for (; store->y < store->h; store->y++) {
			if (i == 0) {
				return 0;
			}
			i--;

			store->raw[store->x][store->y] = (store->raw[store->x][store->y] & 0xfe) | ((byte >> i) & 0x01);
		}
		store->y = 0;
	}

	return 0;
}

static int ILLU_png_store_write_u32(struct ILLU_png_store *store, uint32_t u32)
{
	ILLU_png_store_write_byte(store, (u32 >> 24) & 0x000000ff);
	ILLU_png_store_write_byte(store, (u32 >> 16) & 0x000000ff);
	ILLU_png_store_write_byte(store, (u32 >>  8) & 0x000000ff);
	ILLU_png_store_write_byte(store,  u32        & 0x000000ff);
	return 0;
}

int ILLU_hide_png(struct ILLU_msg *msg, struct ILLU_png_store *store)
{
	store->x = store->y = 0;
	ILLU_png_store_write_byte(store, 'I');
	ILLU_png_store_write_byte(store, 'L');
	ILLU_png_store_write_byte(store, 'L');
	ILLU_png_store_write_byte(store, 'U');
	ILLU_png_store_write_u32(store, msg->len);

	size_t i;
	for (i = 0; i < msg->len; i++) {
		ILLU_png_store_write_byte(store, msg->data[i]);
	}

	return 0;
}

int ILLU_retr_png(struct ILLU_msg *msg, struct ILLU_png_store *store)
{
	store->x = store->y = 0;

	if (ILLU_png_store_read_byte(store) != 'I'
	 || ILLU_png_store_read_byte(store) != 'L'
	 || ILLU_png_store_read_byte(store) != 'L'
	 || ILLU_png_store_read_byte(store) != 'U') {

		fprintf(stderr, "corrupt header\n");
		return 1;
	}

	msg->len = ILLU_png_store_read_u32(store);
	msg->data = calloc(msg->len + 1, sizeof(uint8_t));
	if (!msg->data) {
		return 2;
	}

	size_t i;
	for (i = 0; i < msg->len; i++) {
		msg->data[i] = ILLU_png_store_read_byte(store);
	}

	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "USAGE: ogets (hide|show) <file>\n");
		return 1;
	}

	struct ILLU_msg msg;
	struct ILLU_png_store *png;

	if (strcmp(argv[1], "hide") == 0) {
		/* read data; store it in png pixel data */
		msg.data = calloc(8192, sizeof(uint8_t));
		msg.len = read(0, msg.data, 8192);
		if (msg.len < 0) {
			perror("read error");
			exit(1);
		}
		fprintf(stderr, "read %u bytes of data to hide\n", msg.len);

		png = ILLU_png_store_read(argv[2]);
		if (!png) {
			perror("read error");
			exit(1);
		}

		ILLU_hide_png(&msg, png);
		ILLU_png_store_write(png, "-");

	} else if (strcmp(argv[1], "show") == 0) {
		png = ILLU_png_store_read(argv[2]);
		if (!png) {
			perror("read error");
			exit(1);
		}

		if (ILLU_retr_png(&msg, png) == 0) {
			printf("%s", msg.data);
			if (msg.data[msg.len-1] != '\n') {
				printf("\n");
			}
		}
	}

	return 0;
}
