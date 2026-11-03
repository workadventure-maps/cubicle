ifeq ($(strip $(PVSNESLIB_HOME)),)
export PVSNESLIB_HOME := $(CURDIR)/.tools/pvsneslib
endif

export ROMNAME := cubicles
export ROMTITLE := CUBICLES
export SRC := src
export FASTROM := 1

# music: SNESMOD modules (made by tools/mkmod.py, then edited in a tracker), converted to a soundbank in bank 5
AUDIOFILES := music/mountain_king.it
export SOUNDBANK := music/soundbank

include ${PVSNESLIB_HOME}/devkitsnes/snes_rules

SMCONVFLAGS := -s -o $(SOUNDBANK) -V -b 5
$(SRC)/main.ps: $(SOUNDBANK).asm

# our altered SNESMOD driver (snesmod/sm_spc.asm), uploaded by spcBootMusic in src/music.asm
snesmod/sm_spc.bin: snesmod/sm_spc.asm snesmod/sm_spc.link
	cd snesmod && $(PVSNESLIB_HOME)/devkitsnes/bin/wla-spc700 -o sm_spc.obj sm_spc.asm && \
		$(PVSNESLIB_HOME)/devkitsnes/bin/wlalink -b sm_spc.link sm_spc.bin

$(SRC)/music.obj: snesmod/sm_spc.bin

.PHONY: all gfx test site

all: gfx $(ROMNAME).sfc

GFX_PNG := gfx/blocks_a.png gfx/blocks_b.png gfx/walls.png gfx/font.png gfx/p1.png gfx/p2.png gfx/p3.png gfx/p4.png
GFX_PIC := $(GFX_PNG:.png=.pic)

gfx/.stamp: tools/mkgfx.py assets/font.bmp $(wildcard assets/woka/*.png) $(wildcard assets/sprites/*.png)
	python3 tools/mkgfx.py assets gfx
	touch $@

$(GFX_PNG): gfx/.stamp

gfx/blocks_%.pic: gfx/blocks_%.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -i $<

gfx/walls.pic: gfx/walls.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -i $<

gfx/font.pic: gfx/font.png
	$(GFXCONV) -s 8 -o 4 -u 4 -p -i $<

gfx/p%.pic: gfx/p%.png
	$(GFXCONV) -s 32 -o 16 -u 16 -p -i $<

SFX_WAV := sfx/lock.wav sfx/clear.wav sfx/junk.wav sfx/join.wav sfx/topout.wav
SFX_BRR := $(SFX_WAV:.wav=.brr)

gfx: $(GFX_PIC) $(SFX_BRR)

sfx/.stamp: tools/mksfx.py
	python3 tools/mksfx.py sfx
	touch $@

$(SFX_WAV): sfx/.stamp

sfx/%.brr: sfx/%.wav
	$(BRCONV) -e $< $@

data.obj: $(GFX_PIC) $(SFX_BRR)

test: all
	python3 tests/smoke.py

# the web player: EmulatorJS page and the ROM (python3 -m http.server -d _site)
site: all
	mkdir -p _site
	cp web/index.html $(ROMNAME).sfc _site/
