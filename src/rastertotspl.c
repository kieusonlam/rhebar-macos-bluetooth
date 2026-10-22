/*
 * rastertotspl - CUPS filter: CUPS raster -> TSPL (RHEBAR / Beeprt label printers).
 *
 * Usage (called by CUPS): rastertotspl job user title copies options [file]
 *
 * Self-contained: parses CUPS raster v1/v2/v3 directly, no libcups needed,
 * so it can be cross-compiled for macOS from any host.
 *
 * Printer options arrive through the raster page header (set by the PPD via
 * setpagedevice) and can be overridden by "Key=Value" pairs in argv[5]:
 *   cupsInteger0  Speed          (inch/s, 0 = default)
 *   cupsInteger1  Density + 1    (1..16, 0 = default)
 *   cupsInteger2  MediaTracking  (1 gap, 2 black mark, 3 continuous)
 *   cupsInteger3  GapSize mm     (0 = default 3)
 *   cupsInteger4  Direction      (1 -> DIRECTION 1, 2 -> DIRECTION 0)
 *   cupsInteger5  Halftone       (1 threshold, 2 dither)
 *   cupsInteger6  Invert bitmap  (1 normal, 2 inverted)
 *   cupsInteger7  PostAction     (1 tear, 2 cut each label)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define SYNC_V1 0x52615374u /* RaSt */
#define SYNC_V2 0x52615332u /* RaS2 */
#define SYNC_V3 0x52615333u /* RaS3 */

#define CSPACE_W 0
#define CSPACE_K 3
#define CSPACE_SW 18

typedef struct {
	uint32_t hwres[2];
	uint32_t page_size[2]; /* points */
	uint32_t width, height;
	uint32_t bits_per_color, bits_per_pixel, bytes_per_line;
	uint32_t color_order, color_space;
	uint32_t num_copies;
	float page_size_f[2];
	int32_t ints[16];
	int has_v2;
} page_header;

typedef struct {
	int speed, density, tracking, gap, direction, halftone, invert, post;
} settings;

static FILE *in;
static int swap_bytes;
static int compressed;
static int version;

static int read_all(void *buf, size_t len)
{
	return fread(buf, 1, len, in) == len;
}

static uint32_t get_u32(const unsigned char *p)
{
	uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	if (swap_bytes)
		v = ((v & 0xff) << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
	return v;
}

static float get_f32(const unsigned char *p)
{
	uint32_t v = get_u32(p);
	float f;
	memcpy(&f, &v, 4);
	return f;
}

static int read_sync(void)
{
	unsigned char b[4];
	uint32_t le, be;
	if (!read_all(b, 4))
		return 0;
	le = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	be = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
	if (le == SYNC_V1 || le == SYNC_V2 || le == SYNC_V3) {
		swap_bytes = 0;
		version = le == SYNC_V1 ? 1 : le == SYNC_V2 ? 2 : 3;
	} else if (be == SYNC_V1 || be == SYNC_V2 || be == SYNC_V3) {
		swap_bytes = 1;
		version = be == SYNC_V1 ? 1 : be == SYNC_V2 ? 2 : 3;
	} else {
		return 0;
	}
	compressed = version == 2;
	return 1;
}

static int read_header(page_header *h)
{
	unsigned char b[1796];
	size_t len = version == 1 ? 420 : 1796;
	int i;
	memset(h, 0, sizeof(*h));
	if (!read_all(b, len))
		return 0;
	h->hwres[0] = get_u32(b + 276);
	h->hwres[1] = get_u32(b + 280);
	h->num_copies = get_u32(b + 340);
	h->page_size[0] = get_u32(b + 352);
	h->page_size[1] = get_u32(b + 356);
	h->width = get_u32(b + 372);
	h->height = get_u32(b + 376);
	h->bits_per_color = get_u32(b + 384);
	h->bits_per_pixel = get_u32(b + 388);
	h->bytes_per_line = get_u32(b + 392);
	h->color_order = get_u32(b + 396);
	h->color_space = get_u32(b + 400);
	if (version > 1) {
		h->has_v2 = 1;
		h->page_size_f[0] = get_f32(b + 428);
		h->page_size_f[1] = get_f32(b + 432);
		for (i = 0; i < 16; i++)
			h->ints[i] = (int32_t)get_u32(b + 452 + 4 * i);
	}
	return 1;
}

/* Reads one raster line (v2 compressed or plain), handling line repeats. */
static int line_repeat;
static unsigned char *last_line;

static int read_line(const page_header *h, unsigned char *dst, unsigned char white)
{
	uint32_t bpl = h->bytes_per_line;
	uint32_t unit = h->bits_per_pixel < 8 ? 1 : h->bits_per_pixel / 8;
	uint32_t pos = 0;
	int c;

	if (!compressed)
		return read_all(dst, bpl);

	if (line_repeat > 0) {
		line_repeat--;
		memcpy(dst, last_line, bpl);
		return 1;
	}
	if ((c = fgetc(in)) == EOF)
		return 0;
	line_repeat = c;

	while (pos < bpl) {
		if ((c = fgetc(in)) == EOF)
			return 0;
		if (c == 128) {
			memset(dst + pos, white, bpl - pos);
			pos = bpl;
		} else if (c < 128) {
			uint32_t n = (uint32_t)(c + 1) * unit, k;
			unsigned char px[16];
			if (unit > sizeof(px) || !read_all(px, unit))
				return 0;
			if (n > bpl - pos)
				n = bpl - pos;
			for (k = 0; k < n; k++)
				dst[pos + k] = px[k % unit];
			pos += n;
		} else {
			uint32_t n = (uint32_t)(257 - c) * unit;
			if (n > bpl - pos)
				n = bpl - pos;
			if (!read_all(dst + pos, n))
				return 0;
			pos += n;
		}
	}
	memcpy(last_line, dst, bpl);
	return 1;
}

static int opt_value(const char *opts, const char *key, const char **table, int base)
{
	/* Finds Key=Value in a CUPS options string; returns index+base in table or atoi. */
	size_t kl = strlen(key);
	const char *p = opts;
	while (p && *p) {
		while (*p == ' ')
			p++;
		if (!strncmp(p, key, kl) && p[kl] == '=') {
			const char *v = p + kl + 1;
			size_t vl = strcspn(v, " ");
			int i;
			if (table) {
				for (i = 0; table[i]; i++)
					if (strlen(table[i]) == vl && !strncmp(v, table[i], vl))
						return i + base;
				return -1;
			}
			if (isdigit((unsigned char)*v))
				return atoi(v);
			return -1;
		}
		p += strcspn(p, " ");
	}
	return -1;
}

static void resolve_settings(const page_header *h, const char *opts, settings *s)
{
	static const char *tracking[] = {"Gap", "BlackMark", "Continuous", NULL};
	static const char *direction[] = {"Normal", "Rotate180", NULL};
	static const char *halftone[] = {"Threshold", "Dither", NULL};
	static const char *invert[] = {"False", "True", NULL};
	static const char *post[] = {"Tear", "Cut", NULL};
	int v;

	s->speed = h->ints[0] > 0 ? h->ints[0] : 4;
	s->density = h->ints[1] > 0 ? h->ints[1] - 1 : 8;
	s->tracking = h->ints[2] > 0 ? h->ints[2] : 1;
	s->gap = h->ints[3] > 0 ? h->ints[3] : 3;
	s->direction = h->ints[4] > 0 ? h->ints[4] : 1;
	s->halftone = h->ints[5] > 0 ? h->ints[5] : 1;
	s->invert = h->ints[6] > 0 ? h->ints[6] : 1;
	s->post = h->ints[7] > 0 ? h->ints[7] : 1;

	if ((v = opt_value(opts, "Speed", NULL, 0)) > 0) s->speed = v;
	if ((v = opt_value(opts, "Density", NULL, 0)) >= 0) s->density = v;
	if ((v = opt_value(opts, "MediaTracking", tracking, 1)) > 0) s->tracking = v;
	if ((v = opt_value(opts, "GapSize", NULL, 0)) >= 0) s->gap = v;
	if ((v = opt_value(opts, "PrintDirection", direction, 1)) > 0) s->direction = v;
	if ((v = opt_value(opts, "Halftone", halftone, 1)) > 0) s->halftone = v;
	if ((v = opt_value(opts, "InvertBitmap", invert, 1)) > 0) s->invert = v;
	if ((v = opt_value(opts, "PostAction", post, 1)) > 0) s->post = v;

	if (s->density > 15) s->density = 15;
	if (s->speed > 12) s->speed = 12;
}

static int print_page(const page_header *h, const settings *s, int first)
{
	uint32_t w = h->width, ht = h->height, bpl = h->bytes_per_line;
	uint32_t out_bpl = (w + 7) / 8;
	unsigned char *line = malloc(bpl ? bpl : 1);
	unsigned char *bits = calloc((size_t)out_bpl * (ht ? ht : 1), 1);
	int *err_cur = NULL, *err_next = NULL;
	double wmm, hmm;
	int gray8 = h->bits_per_pixel == 8;
	int black_is_one; /* for 1-bit input */
	unsigned char white;
	uint32_t y, x;

	free(last_line);
	last_line = malloc(bpl ? bpl : 1);
	line_repeat = 0;

	if (!line || !bits || !last_line)
		return 0;
	if (!(h->bits_per_pixel == 1 || gray8) || h->color_order != 0) {
		fprintf(stderr, "ERROR: Unsupported raster format (bpp=%u, order=%u)\n",
		        h->bits_per_pixel, h->color_order);
		return 0;
	}

	/* K: 0 = white; W/SW: 0 = black */
	black_is_one = h->color_space == CSPACE_K;
	white = black_is_one ? 0x00 : 0xFF;

	if (gray8 && s->halftone == 2) {
		err_cur = calloc(w + 2, sizeof(int));
		err_next = calloc(w + 2, sizeof(int));
		if (!err_cur || !err_next)
			return 0;
	}

	for (y = 0; y < ht; y++) {
		unsigned char *row = bits + (size_t)y * out_bpl;
		if (!read_line(h, line, white)) {
			fprintf(stderr, "ERROR: Truncated raster data at line %u\n", y);
			return 0;
		}
		if (!gray8) {
			for (x = 0; x < out_bpl && x < bpl; x++)
				row[x] = black_is_one ? line[x] : (unsigned char)~line[x];
		} else if (!err_cur) {
			for (x = 0; x < w; x++) {
				int lum = black_is_one ? 255 - line[x] : line[x];
				if (lum < 128)
					row[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
			}
		} else {
			int *t;
			memset(err_next, 0, (w + 2) * sizeof(int));
			for (x = 0; x < w; x++) {
				int lum = (black_is_one ? 255 - line[x] : line[x]) + err_cur[x + 1] / 16;
				int out = lum < 128 ? 0 : 255;
				int e = lum - out;
				if (!out)
					row[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
				err_cur[x + 2] += e * 7;
				err_next[x] += e * 3;
				err_next[x + 1] += e * 5;
				err_next[x + 2] += e;
			}
			t = err_cur; err_cur = err_next; err_next = t;
		}
		/* clear padding bits past width */
		if (w & 7)
			row[out_bpl - 1] &= (unsigned char)(0xFF << (8 - (w & 7)));
	}

	/* label size in mm */
	if (h->has_v2 && h->page_size_f[0] > 0 && h->page_size_f[1] > 0) {
		wmm = h->page_size_f[0] * 25.4 / 72.0;
		hmm = h->page_size_f[1] * 25.4 / 72.0;
	} else if (h->page_size[0] && h->page_size[1]) {
		wmm = h->page_size[0] * 25.4 / 72.0;
		hmm = h->page_size[1] * 25.4 / 72.0;
	} else {
		wmm = w * 25.4 / (h->hwres[0] ? h->hwres[0] : 203);
		hmm = ht * 25.4 / (h->hwres[1] ? h->hwres[1] : 203);
	}

	printf("SIZE %.1f mm,%.1f mm\r\n", wmm, hmm);
	if (s->tracking == 1)
		printf("GAP %d mm,0 mm\r\n", s->gap);
	else if (s->tracking == 2)
		printf("BLINE %d mm,0 mm\r\n", s->gap);
	else
		printf("GAP 0 mm,0 mm\r\n");
	if (first) {
		printf("SPEED %d\r\n", s->speed);
		printf("DENSITY %d\r\n", s->density);
		printf("SET TEAR ON\r\n");
		printf("SET CUTTER %s\r\n", s->post == 2 ? "1" : "OFF");
	}
	printf("DIRECTION %d,0\r\n", s->direction == 2 ? 0 : 1);
	printf("REFERENCE 0,0\r\n");
	printf("CLS\r\n");
	printf("BITMAP 0,0,%u,%u,0,", out_bpl, ht);
	/* TSPL BITMAP: bit 0 = print (black), 1 = no print */
	for (y = 0; y < ht; y++) {
		unsigned char *row = bits + (size_t)y * out_bpl;
		for (x = 0; x < out_bpl; x++)
			putchar(s->invert == 2 ? row[x] : (unsigned char)~row[x]);
	}
	printf("\r\nPRINT 1,1\r\n");
	fflush(stdout);

	free(line);
	free(bits);
	free(err_cur);
	free(err_next);
	return 1;
}

int main(int argc, char *argv[])
{
	page_header h;
	settings s;
	int page = 0;
	const char *opts;

	if (argc < 6 || argc > 7) {
		fprintf(stderr, "Usage: %s job user title copies options [file]\n", argv[0]);
		return 1;
	}
	opts = argv[5];
	if (argc == 7) {
		if (!(in = fopen(argv[6], "rb"))) {
			fprintf(stderr, "ERROR: Unable to open raster file\n");
			return 1;
		}
	} else {
		in = stdin;
	}
#ifdef _WIN32
	{
		extern int _setmode(int, int);
		_setmode(0, 0x8000);
		_setmode(1, 0x8000);
	}
#endif

	if (!read_sync()) {
		fprintf(stderr, "ERROR: Not a CUPS raster stream\n");
		return 1;
	}

	while (read_header(&h)) {
		if (h.width == 0 || h.height == 0 || h.bytes_per_line == 0)
			break;
		page++;
		fprintf(stderr, "PAGE: %d 1\n", page);
		fprintf(stderr, "INFO: Printing label %d (%ux%u @ %udpi)\n", page, h.width, h.height, h.hwres[0]);
		resolve_settings(&h, opts, &s);
		if (!print_page(&h, &s, page == 1))
			return 1;
	}

	if (page == 0) {
		fprintf(stderr, "ERROR: No pages found\n");
		return 1;
	}
	fprintf(stderr, "INFO: Ready to print.\n");
	return 0;
}
