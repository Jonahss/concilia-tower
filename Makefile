CC = gcc
CT_VERSION := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
CFLAGS = -Wall -Wextra -std=c11 -O2 -DCT_BUILD_VERSION='"$(CT_VERSION)"' $(shell pkg-config --cflags sdl2 SDL2_ttf)
LDFLAGS = $(shell pkg-config --libs sdl2 SDL2_ttf) -lm

SRC = src/main.c src/ne_resource.c src/sprites.c src/tower.c src/game.c src/people.c src/twr.c src/audio.c src/sound_hook.c src/strings.c src/kwaj_expand.c
# vendored libmspack subset (LGPL) — expands SIMTOWER.EX_ (KWAJ method 3)
VENDOR = src/vendor/mspack/kwajd.c src/vendor/mspack/mszipd.c src/vendor/mspack/lzssd.c src/vendor/mspack/system.c
SRC += $(VENDOR)
CFLAGS += -Isrc/vendor/mspack
OBJ = $(SRC:.c=.o)
BIN = simtower

# Default SIMTOWER.EXE location
EXE_PATH ?= ../OpenSkyscraper/data/SIMTOWER.EXE

.PHONY: all clean run web webserve

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

# Header dependencies — all .c files depend on all .h files in src/
HEADERS = $(wildcard src/*.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN) $(EXE_PATH)

# ---- Web (Emscripten) build ----
# v0 keeps the blocking main loop via ASYNCIFY (SDL_Delay yields to the
# browser); the set_main_loop refactor is queued as a refine step.
# Fonts are embedded; the EXE arrives via upload (see web/shell.html).
WEBDIR = web/dist
web:
	mkdir -p $(WEBDIR)
	sed 's/__CT_VERSION__/$(CT_VERSION)/g' web/shell.html > $(WEBDIR)/.shell.gen.html
	cp web/hero.png $(WEBDIR)/hero.png
	emcc $(SRC) -o $(WEBDIR)/index.html \
	  -std=gnu11 -O2 -DCT_BUILD_VERSION='"$(CT_VERSION)"' \
	  -Isrc/vendor/mspack \
	  -sUSE_SDL=2 -sUSE_SDL_TTF=2 \
	  -sALLOW_MEMORY_GROWTH -sASYNCIFY -sASYNCIFY_STACK_SIZE=32768 \
	  -sSTACK_SIZE=2097152 \
	  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
	  -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS,ENV,addRunDependency,removeRunDependency,ccall \
	  -lidbfs.js \
	  --shell-file $(WEBDIR)/.shell.gen.html \
	  --embed-file web/fonts@/fonts
	rm -f $(WEBDIR)/.shell.gen.html

webserve: web
	@echo "Serving on http://$$(hostname -I | cut -d' ' -f1):8611 (and Tailscale IP)"
	cd $(WEBDIR) && python3 -m http.server 8611

clean:
	rm -f $(OBJ) $(BIN)
	rm -rf $(WEBDIR)
