CC = gcc
CFLAGS = -Wall -Wextra -g -std=gnu11 -Iexternal/linenoise
SOURCES = src/main.c external/linenoise/linenoise.c

mysh: $(SOURCES)
	$(CC) $(CFLAGS) -o mysh $(SOURCES)

clean:
	rm -f mysh
