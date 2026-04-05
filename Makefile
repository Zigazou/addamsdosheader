CC = gcc
CFLAGS = -Wall -Wextra -O2

addamsdosheader: addamsdosheader.c
	$(CC) $(CFLAGS) $< -o $@

tests: addamsdosheader tests.bash
	bash ./tests.bash

clean:
	rm --force addamsdosheader
