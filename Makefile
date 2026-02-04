MADS := mads
CC := gcc
SRC := clients/atari/maze-war.asm
OUT := build/maze-war.xex
HANDLER := NSENGINE.OBX
NET := build/maze-war-net.xex
SERVER := build/maze-war-server
CLIENT := build/maze-war-client

.PHONY: all clean

all: $(NET) $(SERVER) $(CLIENT)

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

clean:
	rm -rf build
