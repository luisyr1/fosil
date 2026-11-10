CFLAGS=-O2 -Wall -Wextra $(shell pkg-config --cflags sdl2 SDL2_ttf)
LDLIBS=$(shell pkg-config --libs sdl2 SDL2_ttf)
ytmusic: ytmusic.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)
