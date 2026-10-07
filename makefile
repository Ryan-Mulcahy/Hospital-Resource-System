SRC     = $(wildcard *.c)
CFLAGS  = -std=c99 -Wall -Wextra

ifeq ($(OS),Windows_NT)
    RAYLIB_BIN ?= C:/raylib/w64devkit/bin
    export PATH := $(RAYLIB_BIN);$(PATH)
    CC      = gcc
    LDFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
    TARGET  = hospital.exe
else
    CC      = cc
    BREW   := $(shell brew --prefix)
    CFLAGS  += -I$(BREW)/include
    LDFLAGS = -L$(BREW)/lib -lraylib
    TARGET  = hospital
endif

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)