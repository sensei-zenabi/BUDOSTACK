CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic

COMMANDS = hello help list
CMD_DIR = commands

all: terminal $(COMMANDS)

terminal: main.c commandparser.c commandparser.h
	$(CC) $(CFLAGS) -o terminal main.c commandparser.c

$(COMMANDS): %: $(CMD_DIR)/%.c
	$(CC) $(CFLAGS) -o $(CMD_DIR)/$@ $<

.PHONY: all clean
clean:
	rm -f terminal $(CMD_DIR)/*
