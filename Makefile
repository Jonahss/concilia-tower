CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 $(shell pkg-config --cflags sdl2 SDL2_ttf)
LDFLAGS = $(shell pkg-config --libs sdl2 SDL2_ttf) -lm

SRC = src/main.c src/ne_resource.c src/sprites.c src/tower.c src/game.c src/people.c src/twr.c src/audio.c src/sound_hook.c src/strings.c
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
	emcc $(SRC) -o $(WEBDIR)/index.html \
	  -std=gnu11 -O2 \
	  -sUSE_SDL=2 -sUSE_SDL_TTF=2 \
	  -sALLOW_MEMORY_GROWTH -sASYNCIFY -sASYNCIFY_STACK_SIZE=32768 \
	  -sSTACK_SIZE=2097152 \
	  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
	  -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS,ENV,addRunDependency,removeRunDependency \
	  -lidbfs.js \
	  --shell-file web/shell.html \
	  --embed-file web/fonts@/fonts

webserve: web
	@echo "Serving on http://$$(hostname -I | cut -d' ' -f1):8611 (and Tailscale IP)"
	cd $(WEBDIR) && python3 -m http.server 8611

clean:
	rm -f $(OBJ) $(BIN)
	rm -rf $(WEBDIR)
