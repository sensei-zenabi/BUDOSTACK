# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -Wno-format-truncation
LDFLAGS =

# Find all .c files recursively
ALL_SOURCES = $(shell find . -type f -name '*.c')
# Non-commands sources (for the main executable)
NON_COMMAND_SOURCES = $(filter-out ./commands/%, $(ALL_SOURCES))
NON_COMMAND_OBJECTS = $(NON_COMMAND_SOURCES:.c=.o)
TARGET = terminal

# Find all .c files in the commands folder
COMMANDS_SRCS = $(shell find ./commands -type f -name '*.c')
# For each such source, create an executable name by stripping the .c extension
COMMANDS_EXES = $(COMMANDS_SRCS:.c=)

# All targets: the main terminal executable and all command executables
ALL_TARGETS = $(TARGET) $(COMMANDS_EXES)

.PHONY: all clean

all: $(ALL_TARGETS)

# Build the main terminal executable from non-commands sources
$(TARGET): $(NON_COMMAND_OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(LDFLAGS) -o $(TARGET) $(NON_COMMAND_OBJECTS)

# For each command executable, link its corresponding object file.
# E.g., ./commands/hello will be built from ./commands/hello.o
$(COMMANDS_EXES): %: %.o
	@echo "Linking $@..."
	$(CC) $(LDFLAGS) -o $@ $<

# Pattern rule: compile any .c file into its corresponding .o file
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Clean: remove the main executable, all command executables,
# and delete all .o files found recursively in the project.
clean:
	rm -f $(TARGET) $(COMMANDS_EXES)
	@echo "Removing all .o files..."
	$(shell find . -type f -name '*.o' -delete)
