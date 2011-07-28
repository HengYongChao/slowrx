CC = gcc

CFLAGS = -Wall -Wextra -std=gnu99 -pedantic

OFLAGS = -O3

all: slowrx


slowrx: common.h common.c slowrx.c gui.c video.c sync.c vis.c modespec.c
	$(CC) $(CFLAGS) $(OFLAGS) common.c modespec.c gui.c video.c vis.c sync.c slowrx.c -o slowrx -lfftw3 -lm `pkg-config --cflags --libs gtk+-2.0` -lgthread-2.0 -lpnglite

clean:
	rm -f *.o
