SRC     = $(wildcard *.c)
CFLAGS  = -std=c99 -Wall -Wextra

ifeq ($(OS),Windows_NT)
    RAYLIB_BIN ?= C:/raylib/w64devkit/bin
    export PATH := $(RAYLIB_BIN);$(PATH)
    CC      = gcc
    LDFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
    TARGET  = hospital.exe
    TESTS   = run_tests.exe
else
    CC      = cc
    BREW   := $(shell brew --prefix)
    CFLAGS  += -I$(BREW)/include
    LDFLAGS = -L$(BREW)/lib -lraylib
    TARGET  = hospital
    TESTS   = run_tests
endif

# The tests use the simulation files only (no drawing, no window)
TEST_SRC = tests/test_core.c patient.c sim.c conditions.c roster.c

$(TARGET): $(SRC) $(wildcard *.h)
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

# make test: build and run the automatic tests in tests/test_core.c
test: $(TESTS)
	./$(TESTS)

$(TESTS): $(TEST_SRC) $(wildcard *.h)
	$(CC) $(CFLAGS) -I. $(TEST_SRC) $(LDFLAGS) -o $(TESTS)

clean:
	rm -f $(TARGET) $(TESTS) log_*.csv