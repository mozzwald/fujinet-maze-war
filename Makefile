MADS := mads
SRC := maze-war.asm
OUT := build/maze-war.xex

.PHONY: all clean

all: $(OUT)

build:
	mkdir -p $@

$(OUT): $(SRC) | build
	$(MADS) $(SRC) -o:$@

clean:
	rm -rf build
