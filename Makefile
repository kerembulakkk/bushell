CC = gcc
CFLAGS = -Wall -Wextra -pedantic
SRC = $(wildcard *.c)

all: bushell

bushell: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o bushell

clean:
	rm -f *.o bushell

.PHONY: all clean