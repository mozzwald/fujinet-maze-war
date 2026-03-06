MADS := mads
CC := gcc
SRC := clients/atari/maze-war.asm
OUT := build/maze-war.xex
HANDLER := NSENGINE.OBX
NET := build/maze-war-net.xex
SERVER := build/maze-war-server
CLIENT := build/maze-war-client
CLIENT_SDL := build/maze-war-client-sdl

.PHONY: all clean

all: $(NET) $(SERVER) $(CLIENT) $(CLIENT_SDL)

build:
	mkdir -p $@

$(OUT): $(SRC) | build
	$(MADS) $(SRC) -o:$@

$(NET): $(HANDLER) $(OUT) | build
	cat $(HANDLER) $(OUT) > $@

$(SERVER): server/main.c | build
	$(CC) -O2 -Wall -Wextra -o $@ $<

$(CLIENT): clients/linux/main.c | build
	$(CC) -O2 -Wall -Wextra -o $@ $< -lncurses

$(CLIENT_SDL): clients/linux/sdl_main.c | build
	$(CC) -O2 -Wall -Wextra -o $@ $< -lSDL

clean:
	rm -rf build
