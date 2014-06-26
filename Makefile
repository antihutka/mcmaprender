CFLAGS=-g -std=gnu99 -I../cNBT -Wall -Werror -Wno-unused

all: mcmaprender

mcmaprender: mcmaprender.o
	$(CC) $(CFLAGS) mcmaprender.o -L../cNBT -lnbt -lz -lpng -o mcmaprender

mcmaprender.o: mcmaprender.c
