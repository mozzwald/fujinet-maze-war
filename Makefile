MADS ?= mads
CC ?= gcc
PYTHON ?= python3
CFLAGS ?= -O2 -Wall -Wextra -pthread
NCURSES_LIBS ?= -lncurses
SDL_CONFIG ?= sdl-config
SDL_CFLAGS ?= $(shell $(SDL_CONFIG) --cflags 2>/dev/null)
SDL_LIBS ?= $(shell $(SDL_CONFIG) --libs 2>/dev/null || echo -lSDL)
SRC := clients/atari/maze-war.asm
OUT := build/maze-war.xex
ATARI_CONFIG := build/maze-war-config.inc
ATARI_CONFIG_DATA := build/maze-war-config-data.inc
HANDLER := NSENGINE.OBX
NET := build/maze-war-net.xex
SERVER := build/maze-war-server
CLIENT := build/maze-war-client
CLIENT_SDL := build/maze-war-client-sdl

# Netstream handler source repo. When present, NSENGINE.OBX is rebuilt from it;
# otherwise the checked-in copy is used as-is. HANDLER_BASE there must stay
# $2800 to match NS_BASE in clients/atari/maze-war.asm.
NETSTREAM_DIR ?= ../fujinet-atari-netstream
NETSTREAM_BUFSIZE ?= 1024

# LAN is the local direct-connect default. QA and PRODUCTION select only a
# generated identity; network routing remains runtime-controlled until 08-07.
HOST ?= 127.0.0.1
ROOM_PORT_BASE ?= 9000
ROOM_COUNT ?= 1
DEFAULT_PORT ?= $(ROOM_PORT_BASE)
LOBBY_BASE ?= https://lobby.fujinet.online
MAZEWAR_CREATOR_ID ?= 0x3022
MAZEWAR_APP_ID ?= 0x03
KILL_LIMIT ?= 5
BUILD_FLAVOR ?= LAN

# Export values through Make's environment rather than interpolating command
# line input into a shell recipe. The generator validates every value before
# it emits MADS source; shell expansion never reparses an environment value.
export HOST ROOM_PORT_BASE ROOM_COUNT DEFAULT_PORT LOBBY_BASE MAZEWAR_CREATOR_ID MAZEWAR_APP_ID KILL_LIMIT BUILD_FLAVOR

.PHONY: all clean test FORCE

all: $(NET) $(SERVER) $(CLIENT) $(CLIENT_SDL)

build:
	mkdir -p $@

ifneq ($(wildcard $(NETSTREAM_DIR)/handler/mads/netstream.s),)
$(HANDLER): $(NETSTREAM_DIR)/handler/mads/netstream.s
	$(MAKE) -C $(NETSTREAM_DIR) mads-handler NETSTREAM_INPUT_BUFSIZE=$(NETSTREAM_BUFSIZE)
	cp $(NETSTREAM_DIR)/build/mads/NSENGINE.OBX $@
endif

$(ATARI_CONFIG) $(ATARI_CONFIG_DATA) &: FORCE scripts/generate_atari_config.py | build
	$(PYTHON) scripts/generate_atari_config.py --output $(ATARI_CONFIG) --data-output $(ATARI_CONFIG_DATA) --host "$$HOST" --room-port-base "$$ROOM_PORT_BASE" --room-count "$$ROOM_COUNT" --default-port "$$DEFAULT_PORT" --lobby-base "$$LOBBY_BASE" --mazewar-creator-id "$$MAZEWAR_CREATOR_ID" --mazewar-app-id "$$MAZEWAR_APP_ID" --kill-limit "$$KILL_LIMIT" --build-flavor "$$BUILD_FLAVOR"

$(OUT): $(SRC) $(ATARI_CONFIG) $(ATARI_CONFIG_DATA) | build
	$(MADS) $(SRC) -t:build/maze-war.lab -o:$@

$(NET): $(HANDLER) $(OUT) | build
	cat $(HANDLER) $(OUT) > $@

$(SERVER): net/tcp_stream.h server/main.c server/lobby_publisher.c server/lobby_publisher.h server/transport_normalize.c server/transport_normalize.h server/transport_stats.c server/transport_stats.h | build
	$(CC) $(CFLAGS) -o $@ server/main.c server/lobby_publisher.c server/transport_normalize.c server/transport_stats.c

$(CLIENT): clients/linux/main.c net/tcp_stream.h | build
	$(CC) $(CFLAGS) -o $@ $< $(NCURSES_LIBS)

$(CLIENT_SDL): clients/linux/sdl_main.c net/tcp_stream.h | build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS)

# Smoke suite. Depends on `all` because several tests exercise the real
# server binary rather than just grepping sources.
test: all
	@fail=0; \
	for t in tests/*.sh; do \
	  if bash "$$t" >/dev/null 2>&1; then \
	    printf '  PASS  %s\n' "$$(basename $$t)"; \
	  else \
	    printf '  FAIL  %s\n' "$$(basename $$t)"; \
	    bash "$$t" 2>&1 | sed 's/^/        /'; \
	    fail=1; \
	  fi; \
	done; \
	$(MAKE) --no-print-directory all >/dev/null || exit $$?; \
	if [ $$fail -ne 0 ]; then echo 'smoke suite FAILED' >&2; exit 1; fi; \
	echo 'smoke suite passed'

clean:
	rm -rf build
