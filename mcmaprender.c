#include "nbt.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <png.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

struct map {
	int cx, cy, scale, dim, sx, sy;
	unsigned char *data;
	time_t mtime;
};

int mapcount;
struct map **maps;
int brdl = INT_MAX, brdr = INT_MIN, brdt = INT_MAX, brdb = INT_MIN;
int imagex, imagey;
uint32_t *imagedata;
char *outfile, *mappath;

int pal[][3] = {
	{214, 190, 150},
	{127, 178,  56},
	{247, 233, 163},
	{167, 167, 167},
	{255,   0,   0},
	{160, 160, 255},
	{167, 167, 167},
	{  0, 124,   0},
	
	{255, 255, 255},
	{164, 168, 184},
	{183, 106,  47},
	{112, 112, 112},
	{ 64,  64, 255},
	{104,  83,  50},
	{255, 252, 245},
	{216, 127,  51},
	
	{178,  76, 216},
	{102, 153, 216},
	{229, 229,  51},
	{127, 204,  25},
	{242, 127, 165},
	{ 76,  76,  76},
	{153, 153, 153},
	{ 76, 127, 153},
	
	{127,  63, 178},
	{ 51,  76, 178},
	{102,  76,  51},
	{102, 127,  51},
	{153,  51,  51},
	{ 25,  25,  25},
	{250, 238,  77},
	{ 93, 219, 213},
	
	{ 74, 128, 255},
	{  0, 217,  58},
	{ 21,  20,  31},
	{112,   2,   0}
};

int shades[] = {180, 220, 255, 135};

int getcolor(int cid)
{
	int base = cid / 4;
	int shade = cid % 4;
	int mul = shades[shade];
	if (base >= sizeof(pal)/sizeof(pal[0])) return 0;
	int r = pal[base][0] * mul / 255;
	int g = pal[base][1] * mul / 255;
	int b = pal[base][2] * mul / 255;
	return r + (g << 8) + (b << 16) + (255 << 24);
}

/* The number of bytes to process at a time */
#define CHUNK_SIZE 4096

/*
 * Reads a whole file into a buffer. Returns a NULL buffer and sets errno on
 * error.
 */
static struct buffer read_file(FILE* fp)
{
    struct buffer ret = BUFFER_INIT;

    size_t bytes_read;

    do {
        if(buffer_reserve(&ret, ret.len + CHUNK_SIZE))
            return (errno = NBT_EMEM), buffer_free(&ret), BUFFER_INIT;

        bytes_read = fread(ret.data + ret.len, 1, CHUNK_SIZE, fp);
        ret.len += bytes_read;

        if(ferror(fp))
            return (errno = NBT_EIO), buffer_free(&ret), BUFFER_INIT;

    } while(!feof(fp));

    return ret;
}

void perr(char *s)
{
	perror(s);
	exit(EXIT_FAILURE);
}

void err(char *s)
{
	puts(s);
	exit(EXIT_FAILURE);
}

int max(int a, int b)
{
	return a<b ? b:a;
}

int min(int a, int b)
{
	return a>b ? b:a;
}

int getmapcount()
{
	char in[64];
	snprintf(in, sizeof(in), "%s/idcounts.dat", mappath);
	FILE *f = fopen(in, "r");
	if (!f) perr("fopen");
	struct buffer fb = read_file(f);
	fclose(f);
	if (fb.data == NULL) err("Cannot open idcounts.dat");
	nbt_node *r = nbt_parse(fb.data, fb.len);
	if(errno != NBT_OK) err("Parse error");
	
	//char* str = nbt_dump_ascii(r);
	//printf("%s", str);
	
	nbt_node *cnt = nbt_find_by_name(r, "map");
	if (cnt->type != TAG_SHORT) err("Invalid tag type");
	int count = cnt->payload.tag_short;
	nbt_free(r);
	
	return count+1;
}

void loadmap(int mapid)
{
	maps[mapid] = NULL;
	char mn[64];
	snprintf(mn, sizeof(mn), "%s/map_%d.dat", mappath, mapid);
	printf("%s: ", mn);
	nbt_node *root = nbt_parse_path(mn);
	if (!root) { printf("Cannot parse\n"); return; }
	
	struct map *m = malloc(sizeof(*m));
	struct stat st;
	stat(mn, &st);
	m->mtime = st.st_mtime;
	
	maps[mapid] = m;
	m->scale = nbt_find_by_name(root, "scale")->payload.tag_byte;
	m->dim = nbt_find_by_name(root, "dimension")->payload.tag_byte;
	m->sy = nbt_find_by_name(root, "height")->payload.tag_short;
	m->sx = nbt_find_by_name(root, "width")->payload.tag_short;
	m->cx = nbt_find_by_name(root, "xCenter")->payload.tag_int;
	m->cy = nbt_find_by_name(root, "zCenter")->payload.tag_int;
	nbt_node *ct = nbt_find_by_name(root, "colors");
	int ctlen = ct->payload.tag_byte_array.length;
	if (ctlen == m->sx * m->sy) {
		m->data = malloc(ctlen);
		memcpy(m->data, ct->payload.tag_byte_array.data, ctlen);
	} else {
		m->data = NULL;
	}
	printf("dim %d scale %d size %dx%d center %d,%d datalen %d mtime %d\n",
		m->dim, m->scale, m->sx, m->sy, m->cx, m->cy, ctlen, (int)m->mtime);
	nbt_free(root);
}

void isvalid(int mapid)
{
	struct map *m = maps[mapid];
	if (m->scale > 4 || m->dim != 0 || m->sx != 128 || m->sy != 128 || m->data == NULL) {
		free(m->data);
		free(m);
		maps[mapid] = NULL;
		printf ("Map %d invalid\n", mapid);
	}
}

void mapbnd(int mapid)
{
	struct map *m = maps[mapid];
	if (!m) return;
	int halfsize = 64 << m->scale;
	
	int lb=m->cx - halfsize;
	int rb=m->cx + halfsize;
	int tb=m->cy - halfsize;
	int bb=m->cy + halfsize;
	brdl = min(brdl, lb);
	brdr = max(brdr, rb);
	brdt = min(brdt, tb);
	brdb = max(brdb, bb);
}

void drawpixel(int x, int y, int scale, int mapdata)
{
	int ix = x-brdl;
	int iy = y-brdt;
	int px, py;
	if (mapdata == 0) return;
	//printf("mapdata %d %d %d\n", ix, iy, mapdata);
	//imagedata[ix+imagex*iy] = mapdata + (255 << 24);
	for (px=0; px<(1<<scale); px++) {
		for (py=0; py<(1<<scale); py++) {
			imagedata[(ix+px)+imagex*(iy+py)] = getcolor(mapdata);
		}
	}
}

void rendermap(struct map *map)
{
	int x, y;
	int sizemul = 1 << map->scale;
	for (x=0; x<128; x++) {
		for (y=0; y<128; y++) {
			drawpixel(map->cx+(x-64)*sizemul, map->cy+(y-64)*sizemul, map->scale, map->data[x+128*y]);
		}
	}
}

void writepng()
{
	png_image pi;
	memset(&pi, 0, sizeof(pi));
	pi.version = PNG_IMAGE_VERSION;
	pi.width = imagex;
	pi.height = imagey;
	pi.format = PNG_FORMAT_RGBA;
	int rv = png_image_write_to_file(&pi, outfile, 0, imagedata, imagex*4, NULL);
	printf("%d\n", rv);
	if (!rv) printf("pngtopng: error: %s\n", pi.message);
}

void clearmap()
{
	int i, c = getcolor(0);
	for (i=0; i<imagex*imagey; i++) {
		imagedata[i] = c;
	}
}

int mapq_compare(const void *a, const void *b)
{
	int aa = *(int*)a;
	int bb = *(int*)b;
	if (!maps[aa] || !maps[bb]) return aa - bb;
	if (maps[aa]->scale != maps[bb]->scale) return maps[bb]->scale - maps[aa]->scale;
	return maps[aa]->mtime - maps[bb]->mtime;
}

int main(int argc, char **argv)
{
	int i, z;
	
	mappath = (argc >= 2) ? argv[1] : ".";
	outfile = (argc >= 3) ? argv[2] : "map.png";
	
	mapcount = getmapcount();
	printf("Last map: %d\n", mapcount-1);
	maps = malloc(sizeof(*maps)*(mapcount));
	for (i=0; i<mapcount; i++) {
		loadmap(i);
		isvalid(i);
		mapbnd(i);
	}
	
	imagex = brdr - brdl;
	imagey = brdb - brdt;
	printf("Map: %d %d %d %d => %dx%d\n", brdl, brdr, brdt, brdb, imagex, imagey);
	imagedata = malloc(4*imagex*imagey);
	//memset(imagedata, 0, 4*imagex*imagey);
	clearmap();
	
	int *mapq = malloc(sizeof(*mapq)*mapcount);
	for (i=0; i<mapcount; i++) {
		mapq[i] = i;
	}
	qsort(mapq, mapcount, sizeof(*mapq), mapq_compare);
	for (i=0; i<mapcount; i++) {
		printf("Rendering map %d\n", mapq[i]);
		rendermap(maps[mapq[i]]);
	}
	writepng();
	return 0;
}
