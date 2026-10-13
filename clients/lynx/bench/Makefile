CL65 ?= cl65
CC ?= cc
CC65_ROOT ?= /usr/share/cc65
TARGET = ../build/render-bench.lnx
SOURCES = main.c playfield.c netstream.c clock.s ../src/frame.c ../src/protocol.c reference_render.c
.PHONY: all test crc
crc:
	mkdir -p ../build
	$(CL65) -t lynx -O -I ../src -I $(CC65_ROOT)/include \
		--asm-include-dir $(CC65_ROOT)/asminc -L $(CC65_ROOT)/lib -C ../src/lynx.cfg \
		-o ../build/crc-bench.lnx crc_main.c clock.s ../src/frame.c ../src/netstream.c ../src/protocol.c ../src/command.c
all:
	mkdir -p ../build
	$(CL65) -t lynx -O -I ../src -I $(CC65_ROOT)/include \
		--asm-include-dir $(CC65_ROOT)/asminc -L $(CC65_ROOT)/lib -C ../src/lynx.cfg \
		-m ../build/render-bench.map -o $(TARGET) $(SOURCES)

test:
	mkdir -p ../build
	$(CC) -std=c99 -Wall -Wextra -Werror -DLYNX_BENCH_HOST -I ../src \
		-o ../build/suzy-pixel-test pixel_test.c playfield.c
	../build/suzy-pixel-test
