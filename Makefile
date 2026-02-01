MADS := mads
SRC := maze-war.asm
OUT := build/maze-war.xex
HANDLER := NSENGINE.OBX
NET := build/maze-war-net.xex

.PHONY: all clean

all: $(NET)

build:
	mkdir -p $@

$(OUT): $(SRC) | build
	$(MADS) $(SRC) -o:$@

$(NET): $(HANDLER) $(OUT) | build
	cat $(HANDLER) $(OUT) > $@

clean:
	rm -rf build
