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
# Diagnostic only: bypass local interpolation to isolate hardware release drift.
LOCAL_CELL_TEST ?= 0
# Hardware fault isolation only; the checksum scans intentionally slow menus.
BOOT_GUARD ?= 0
TARGET := build/maze-war-lynx.lnx
MAP := build/maze-war-lynx.map
SOURCES := src/main.c src/command.c src/setup.c src/startup.c src/netstream.c src/frame.c src/protocol.c src/render.c src/playfield.c src/phase_scratch.c src/sound.c src/lynx_tgi.s src/graphics_buffers.s
GUARD_FLAGS :=
ifeq ($(BOOT_GUARD),1)
SOURCES += src/boot_guard.s
# Only the final two route points are read by the bounded renderer. Compact
# this diagnostic's presentation history to fit the guard with the 2 KB stack.
GUARD_FLAGS := -D LYNX_BOOT_GUARD=1 -D GAME_MOTION_POINTS=2 -u _boot_expected
endif

.PHONY: all clean test art check-art FORCE
all: $(TARGET)

art:
	python3 tools/generate_art.py

check-art:
	python3 tools/generate_art.py --check

# One generator owns all outputs; phony prerequisite also repairs deleted files.
src/maze_sprites.h src/death_sprites.h src/font_art.h src/art_pixels.h: art

build:
	mkdir -p build

# Endpoint changes must relink even if the C sources are unchanged.
$(TARGET): $(SOURCES) src/netstream.h src/frame.h src/protocol.h src/render.h src/playfield.h src/colors.h src/playfield_internal.h src/death_sprites.h src/maze_sprites.h src/font_art.h src/command.h src/setup.h src/phase_scratch.h src/startup.h src/sound.h src/boot_guard.h src/lynx.cfg tools/seal_boot_guard.py tools/check_graphics_layout.py Makefile FORCE | build
	$(CL65) -t lynx -O -I src -I $(CC65_ROOT)/include \
		--asm-include-dir $(CC65_ROOT)/asminc -L $(CC65_ROOT)/lib \
		-C src/lynx.cfg -vm -m $(MAP) \
		-D 'SERVER_HOST="$(SERVER_HOST)"' -D SERVER_PORT=$(SERVER_PORT) \
		-D 'LOBBY_BASE="$(LOBBY_BASE)"' -D 'LOBBY_PLATFORM="$(LOBBY_PLATFORM)"' \
		-D ROOM_PORT_BASE=$(ROOM_PORT_BASE) -D ROOM_COUNT=$(ROOM_COUNT) \
		-D LYNX_LOCAL_CELL_TEST=$(LOCAL_CELL_TEST) \
		$(GUARD_FLAGS) \
		-o $@ $(SOURCES)
	python3 tools/check_graphics_layout.py $@ $(MAP)
ifeq ($(BOOT_GUARD),1)
	python3 tools/seal_boot_guard.py $@ $(MAP)
endif

test: art | build
	python3 tools/generate_art.py --check
	python3 tools/test_art.py
	node --test ../../tools/charsetter/maze-model.test.js
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_COMMAND_HOST -I src \
		-D 'SERVER_HOST="fujinet.online"' -D SERVER_PORT=5025 \
		-o build/setup_test tests/setup_test.c src/setup.c src/command.c src/phase_scratch.c
	./build/setup_test
	$(CC) -std=c99 -Wall -Wextra -Werror -I src \
		-o build/frame_test tests/frame_test.c src/frame.c
	./build/frame_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_NETSTREAM_HOST -I src -I tests \
		-o build/netstream_test tests/netstream_test.c src/netstream.c src/frame.c src/protocol.c
	./build/netstream_test
	$(CC) -std=c99 -Wall -Wextra -Werror -I src \
		-o build/protocol_test tests/protocol_test.c src/protocol.c
	./build/protocol_test
	$(CC) -std=c99 -Wall -Wextra -Werror -I src \
		-o build/input_test tests/input_test.c src/protocol.c
	./build/input_test
	$(CC) -std=c99 -Wall -Wextra -Werror -I src \
		-o build/retry_test tests/retry_test.c src/protocol.c
	./build/retry_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/motion_test tests/motion_test.c tests/suzy_host.c src/playfield.c src/phase_scratch.c src/protocol.c
	./build/motion_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/observation_test tests/observation_test.c tests/suzy_host.c src/playfield.c src/phase_scratch.c src/protocol.c
	./build/observation_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/playfield_test tests/playfield_test.c tests/suzy_host.c src/playfield.c src/phase_scratch.c src/protocol.c
	./build/playfield_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/cache_test tests/cache_test.c tests/suzy_host.c \
		tests/reference_playfield.c src/playfield.c src/phase_scratch.c src/protocol.c
	./build/cache_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/death_test tests/death_test.c tests/suzy_host.c \
		tests/reference_playfield.c src/protocol.c src/playfield.c src/phase_scratch.c
	./build/death_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_RENDER_HOST -I src -I tests \
		-o build/round_test tests/round_test.c tests/suzy_host.c src/protocol.c src/playfield.c src/phase_scratch.c
	./build/round_test
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_SOUND_HOST -I src -I tests \
		-o build/sound_test tests/sound_test.c src/protocol.c src/sound.c
	./build/sound_test

clean:
	rm -rf build
