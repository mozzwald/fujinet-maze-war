MADS ?= mads
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
NCURSES_LIBS ?= -lncurses
SDL_CONFIG ?= sdl-config
SDL_CFLAGS ?= $(shell $(SDL_CONFIG) --cflags 2>/dev/null)
SDL_LIBS ?= $(shell $(SDL_CONFIG) --libs 2>/dev/null || echo -lSDL)
SRC := clients/atari/maze-war.asm
OUT := build/maze-war.xex
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

.PHONY: all clean test

all: $(NET) $(SERVER) $(CLIENT) $(CLIENT_SDL)

build:
	mkdir -p $@

ifneq ($(wildcard $(NETSTREAM_DIR)/handler/mads/netstream.s),)
$(HANDLER): $(NETSTREAM_DIR)/handler/mads/netstream.s
	$(MAKE) -C $(NETSTREAM_DIR) mads-handler NETSTREAM_INPUT_BUFSIZE=$(NETSTREAM_BUFSIZE)
	cp $(NETSTREAM_DIR)/build/mads/NSENGINE.OBX $@
endif

$(OUT): $(SRC) | build
	$(MADS) $(SRC) -t:build/maze-war.lab -o:$@

$(NET): $(HANDLER) $(OUT) | build
	cat $(HANDLER) $(OUT) > $@

$(SERVER): server/main.c server/transport_normalize.c server/transport_normalize.h server/transport_stats.c server/transport_stats.h | build
	$(CC) $(CFLAGS) -o $@ server/main.c server/transport_normalize.c server/transport_stats.c

$(CLIENT): clients/linux/main.c | build
	$(CC) $(CFLAGS) -o $@ $< $(NCURSES_LIBS)

$(CLIENT_SDL): clients/linux/sdl_main.c | build
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
	if [ $$fail -ne 0 ]; then echo 'smoke suite FAILED' >&2; exit 1; fi; \
	echo 'smoke suite passed'

clean:
	rm -rf build
