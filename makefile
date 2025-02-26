# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -Wno-format-truncation
LDFLAGS = -lasound

# Find all .c files recursively
ALL_SOURCES = $(shell find . -type f -name '*.c')
# Non-commands and non-app sources (for the main executable)
NON_COMMAND_SOURCES = $(filter-out ./commands/% ./apps/%, $(ALL_SOURCES))
NON_COMMAND_OBJECTS = $(NON_COMMAND_SOURCES:.c=.o)
TARGET = terminal

# Find all .c files in the commands folder
COMMANDS_SRCS = $(shell find ./commands -type f -name '*.c')
COMMANDS_EXES = $(COMMANDS_SRCS:.c=)

# Find all .c files in the apps folder
APPS_SRCS = $(shell find ./apps -type f -name '*.c')
APPS_EXES = $(APPS_SRCS:.c=)

# All targets: the main terminal executable, all command executables, and all app executables
ALL_TARGETS = $(TARGET) $(COMMANDS_EXES) $(APPS_EXES)

.PHONY: all clean

all: $(ALL_TARGETS)

# Build the main terminal executable from non-command and non-app sources
$(TARGET): $(NON_COMMAND_OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(NON_COMMAND_OBJECTS) $(LDFLAGS) -o $(TARGET)

# For each command or app executable, link its corresponding object file.
$(COMMANDS_EXES) $(APPS_EXES): %: %.o
	@echo "Linking $@..."
	$(CC) $< $(LDFLAGS) -o $@

# Pattern rule: compile any .c file into its corresponding .o file
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Clean: remove the main executable, all command and app executables,
# and delete all .o files found recursively in the project.
clean:
	rm -f $(TARGET) $(COMMANDS_EXES) $(APPS_EXES)
	@echo "Removing all .o files..."
	$(shell find . -type f -name '*.o' -delete)
