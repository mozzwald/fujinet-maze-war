MADS := mads
CC := gcc
SERVER := build/udp_test_server
SRC := maze-war.asm
OUT := build/maze-war.xex
HANDLER := NSENGINE.OBX
NET := build/maze-war-net.xex

.PHONY: all clean

all: $(NET) $(SERVER)

build:
	mkdir -p $@

$(OUT): $(SRC) | build
	$(MADS) $(SRC) -o:$@

$(NET): $(HANDLER) $(OUT) | build
	cat $(HANDLER) $(OUT) > $@

$(SERVER): tests/udp_test_server.c | build
	$(CC) -O2 -Wall -Wextra -o $@ $<

clean:
	rm -rf build
