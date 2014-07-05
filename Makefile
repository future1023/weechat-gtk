EXEC     = test
CC       = gcc -fdiagnostics-color=always

CFLAGS   = -std=c99 -O3 -Wall -Wextra -Wpedantic -Wstrict-aliasing
CFLAGS  += $(shell pkg-config --cflags gio-2.0)
LDFLAGS  = $(shell pkg-config --libs   gio-2.0)

SRC      = $(wildcard *.c)
OBJ      = $(SRC:.c=.o)

all: $(EXEC)

${EXEC}: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: clean mrproper

clean:
	@rm -rf *.o

mrproper: clean
	@rm -rf $(EXEC)
			