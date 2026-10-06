CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -I$(shell brew --prefix)/include
LDFLAGS = -L$(shell brew --prefix)/lib -lraylib
SRC     = $(wildcard *.c)

hospital: $(SRC)
      $(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o hospital

run: hospital
      ./hospital

clean:
      rm -f hospital