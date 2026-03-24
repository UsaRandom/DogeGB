PROJECT_NAME = DogeGB
CC = ./gbdk/bin/lcc

MODE ?= Doge

LOWER_MODE := $(shell echo $(MODE) | tr A-Z a-z)

DEFAULT_MODE := 0

ifeq ($(findstring doge,$(LOWER_MODE)),doge)
  CANONICAL_MODE := Doge
else ifeq ($(findstring pepe,$(LOWER_MODE)),pepe)
  CANONICAL_MODE := Pepe
  DEFAULT_MODE := 2
else ifeq ($(findstring bells,$(LOWER_MODE)),bells)
  CANONICAL_MODE := Bells
  DEFAULT_MODE := 1
else
  CANONICAL_MODE := Doge
  $(warning Unknown MODE='$(MODE)' → using Doge)
endif

CANONICAL_MODE_UPPER := $(shell echo $(CANONICAL_MODE) | tr a-z A-Z)

# Compiler flags
CFLAGS = -msm83:gb \
         -Wl-yt0x1B \
         -Wl-yo16 \
         -Wl-ya1 \
         -Wb-ext=.rel \
         -Wm-yC \
         -I. \
         -Ibuild \
         -Isrc/crypto \
         -Iqr \
         -Isrc \
         -DDEFAULT_MODE=$(DEFAULT_MODE)

# Optimization flags (passed via -Wf)
OPTFLAGS = # -Wf--opt-code-speed \
           #-Wf--max-allocs-per-node1000

# Source files
SRC = src/main.c \
      src/qr/qrcodegen.c \
      src/qr/qr_wrapper.c \
      src/states_slot.c \
      src/states_bonk.c \
      src/states_generation.c \
      src/states_wallet_menu.c \
      src/states_wordtest.c \
      src/states_testing.c \
      src/assets/arrow.c \
      src/assets/progress_bar.c \
      src/assets/keyboard.c \
      src/assets/keyboard_lightgrey.c \
      src/assets/bork.c \
      src/assets/pepelogo.c \
      src/assets/cheems_idle.c \
      src/assets/cheems_bonk.c \
      src/assets/cheems_selfbonk.c \
      src/assets/pepe_idle.c \
      src/assets/pepe_bonk.c \
      src/assets/pepe_selfbonk.c \
      src/assets/abutton.c \
      src/assets/bbutton.c \
      src/assets/dpadbutton_up.c \
      src/assets/dpadbutton_down.c \
      src/assets/dpadbutton_left.c \
      src/assets/dpadbutton_right.c \
      src/assets/dogecoin.c \
      src/assets/pepecoin.c \
      src/assets/bellscoin.c \
      src/assets/chksum.c \
      src/splash.c \
      src/wallet.c \
      src/bonktime.c \
      src/word_input.c \
      src/menu.c \
      src/draw.c \
      src/progress.c \
      src/crypto/bip39_wordlist.c \
      src/crypto/bip39_words_1.c \
      src/crypto/bip39_words_2.c \
      src/crypto/sha256.c \
      src/crypto/mnemonic.c \
      src/crypto/pbkdf2.c \
      src/crypto/sha512.c \
      src/crypto/sha512_transform.c \
      src/crypto/sha512_constants.c \
      src/crypto/hd_wallet.c \
      src/crypto/secp256k1.c \
      src/crypto/hmac.c \
      src/crypto/ripemd160.c \
      src/crypto/entropy_data.c \
      src/bitrot_rom.c \
      src/bitrot_save.c

.PHONY: entropy test clean postclean assets savedata

.DEFAULT_GOAL := all

bank = 6

savedata:
	mkdir -p build
	$(CC) -Wf-ba0 -c -o build/wallet_sram.o src/wallet_sram.c

assets:
	./gbdk/bin/png2asset ./raw_assets/pepe_idle.png -map -noflip -tile_origin 243 -b $(bank) -o ./src/assets/pepe_idle.c
	./gbdk/bin/png2asset ./raw_assets/pepe_bonk.png -map -noflip -tile_origin 137 -b $(bank) -o ./src/assets/pepe_bonk.c
	./gbdk/bin/png2asset ./raw_assets/pepe_selfbonk.png -map -noflip -tile_origin 202 -b $(bank) -o ./src/assets/pepe_selfbonk.c
	./gbdk/bin/png2asset ./raw_assets/cheems_idle.png -map -noflip -tile_origin 243 -b $(bank) -o ./src/assets/cheems_idle.c
	./gbdk/bin/png2asset ./raw_assets/cheems_bonk.png -map -noflip -tile_origin 137 -b $(bank) -o ./src/assets/cheems_bonk.c
	./gbdk/bin/png2asset ./raw_assets/cheems_selfbonk.png -map -noflip -tile_origin 202 -b $(bank) -o ./src/assets/cheems_selfbonk.c
	./gbdk/bin/png2asset ./raw_assets/abutton.png -map -noflip -tile_origin 186 -b $(bank) -o ./src/assets/abutton.c
	./gbdk/bin/png2asset ./raw_assets/bbutton.png -map -noflip -tile_origin 190 -b $(bank) -o ./src/assets/bbutton.c
	./gbdk/bin/png2asset ./raw_assets/dpadbutton_up.png -map -noflip -tile_origin 194 -b $(bank) -o ./src/assets/dpadbutton_up.c
	./gbdk/bin/png2asset ./raw_assets/dpadbutton_down.png -map -noflip -tile_origin 80 -b $(bank) -o ./src/assets/dpadbutton_down.c
	./gbdk/bin/png2asset ./raw_assets/dpadbutton_right.png -map -noflip -tile_origin 198 -b $(bank) -o ./src/assets/dpadbutton_right.c
	./gbdk/bin/png2asset ./raw_assets/dpadbutton_left.png -map -noflip -tile_origin 85 -b $(bank) -o ./src/assets/dpadbutton_left.c
	./gbdk/bin/png2asset ./raw_assets/keyboard.png -map -noflip -tile_origin 88 -b 5 -use_map_attributes -o ./src/assets/keyboard.c
	./gbdk/bin/png2asset ./raw_assets/keyboard_lightgrey.png -map -noflip -tile_origin 160 -b 5 -use_map_attributes -o ./src/assets/keyboard_lightgrey.c
	./gbdk/bin/png2asset ./raw_assets/pepecoin.png -tile_origin 202 -b 1 -o ./src/assets/pepecoin.c
	./gbdk/bin/png2asset ./raw_assets/bellscoin.png -tile_origin 202 -b 1 -o ./src/assets/bellscoin.c
	./gbdk/bin/png2asset ./raw_assets/dogecoin.png -tile_origin 202 -b 1 -o ./src/assets/dogecoin.c
	./gbdk/bin/png2asset ./raw_assets/bork.png -use_map_attributes -map -noflip -tile_origin 0 -b 7 -o ./src/assets/bork.c
	./gbdk/bin/png2asset ./raw_assets/pepelogo.png -use_map_attributes -map -noflip -tile_origin 0 -b 7 -o ./src/assets/pepelogo.c
	./gbdk/bin/png2asset ./raw_assets/chksum.png -map -noflip -tile_origin 150 -b 1 -o ./src/assets/chksum.c


test:
	cd test && make
	python3 test/test_crypto.py 100


entropy:
	python3 tools/generate_entropy.py


clean:
	rm -f $(CANONICAL_MODE)GB.gb *.map *.sym *.noi *.ihx *.lk *.adb

postclean:
	rm build/*.asm build/*.lst build/*.o build/*.sym

build/$(CANONICAL_MODE)GB.gb: $(SRC) build/wallet_sram.o
	$(CC) $(CFLAGS) $(OPTFLAGS) -o $@ $^
	python3 tools/patch_bitrot.py $@

all: clean test assets savedata build/$(CANONICAL_MODE)GB.gb postclean
