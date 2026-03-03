CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 $(shell pkg-config --cflags sdl2)
LDFLAGS = $(shell pkg-config --libs sdl2) -lm

SRC = src/main.c src/ne_resource.c src/sprites.c src/tower.c
OBJ = $(SRC:.c=.o)
BIN = simtower

# Default SIMTOWER.EXE location
EXE_PATH ?= ../OpenSkyscraper/data/SIMTOWER.EXE

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN) $(EXE_PATH)

clean:
	rm -f $(OBJ) $(BIN)
