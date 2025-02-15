CC = gcc
CFLAGS = -std=c11 -Wall

# Define commands directory and the list of commands
COMMANDS = hello help
CMD_DIR = commands

all: terminal $(COMMANDS)

# Compile the main terminal program
terminal: main.c commandparser.c
	$(CC) $(CFLAGS) -o terminal main.c commandparser.c

# Compile each command in the commands directory
$(COMMANDS): %: $(CMD_DIR)/%.c
	$(CC) $(CFLAGS) -o $(CMD_DIR)/$@ $<

clean:
	rm -f terminal $(CMD_DIR)/*
