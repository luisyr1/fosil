CFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags sdl2 SDL2_ttf glesv2 libdrm)
LDLIBS = $(shell pkg-config --libs sdl2 SDL2_ttf glesv2 libdrm) -lpam -lcrypt -lpthread -lm
fosil: fosil.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)
