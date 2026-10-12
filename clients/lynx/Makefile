CL65 ?= cl65
CC ?= cc
-include config.mk
CC65_ROOT ?= /usr/share/cc65
SERVER_HOST ?= 127.0.0.1
SERVER_PORT ?= 9000
LOBBY_BASE ?= https://lobby.fujinet.online
LOBBY_PLATFORM ?= atari
ROOM_PORT_BASE ?= $(SERVER_PORT)
ROOM_COUNT ?= 4
TARGET := build/maze-war-lynx.lnx
MAP := build/maze-war-lynx.map
SOURCES := src/main.c src/command.c src/setup.c src/startup.c src/netstream.c src/frame.c src/protocol.c src/render.c src/playfield.c src/sound.c

.PHONY: all clean test FORCE
all: $(TARGET)

build:
	mkdir -p build

# Endpoint changes must relink even if the C sources are unchanged.
$(TARGET): $(SOURCES) src/netstream.h src/frame.h src/protocol.h src/render.h src/playfield.h src/colors.h src/playfield_internal.h src/death_art.h src/command.h src/setup.h src/startup.h src/sound.h src/lynx.cfg Makefile FORCE | build
	$(CL65) -t lynx -O -I src -I $(CC65_ROOT)/include \
		--asm-include-dir $(CC65_ROOT)/asminc -L $(CC65_ROOT)/lib \
		-C src/lynx.cfg -m $(MAP) \
		-D 'SERVER_HOST="$(SERVER_HOST)"' -D SERVER_PORT=$(SERVER_PORT) \
		-D 'LOBBY_BASE="$(LOBBY_BASE)"' -D 'LOBBY_PLATFORM="$(LOBBY_PLATFORM)"' \
		-D ROOM_PORT_BASE=$(ROOM_PORT_BASE) -D ROOM_COUNT=$(ROOM_COUNT) \
		-o $@ $(SOURCES)

test: | build
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_COMMAND_HOST -I src \
		-D 'SERVER_HOST="fujinet.online"' -D SERVER_PORT=5025 \
		-o build/setup_test tests/setup_test.c src/setup.c src/command.c
	./build/setup_test
	$(CC) -std=c99 -Wall -Wextra -Werror -I src \
		-o build/frame_test tests/frame_test.c src/frame.c
	./build/frame_test
	$(CC) -std=c99 -Wall -Wextra -Werror -I src \
		-o build/protocol_test tests/protocol_test.c src/protocol.c
	./build/protocol_test
	$(CC) -std=c99 -Wall -Wextra -Werror -I src \
		-o build/input_test tests/input_test.c src/protocol.c
	./build/input_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/motion_test tests/motion_test.c tests/suzy_host.c src/playfield.c src/protocol.c
	./build/motion_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/playfield_test tests/playfield_test.c tests/suzy_host.c src/playfield.c src/protocol.c
	./build/playfield_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/cache_test tests/cache_test.c tests/suzy_host.c \
		tests/reference_playfield.c src/playfield.c src/protocol.c
	./build/cache_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/death_test tests/death_test.c tests/suzy_host.c \
		tests/reference_playfield.c src/protocol.c src/playfield.c
	./build/death_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/round_test tests/round_test.c tests/suzy_host.c src/protocol.c src/playfield.c
	./build/round_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_SOUND_HOST -I src -I tests \
		-o build/sound_test tests/sound_test.c src/protocol.c src/sound.c
	./build/sound_test

clean:
	rm -rf build
