# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic
SDL2_CFLAGS ?= $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS ?= $(shell pkg-config --libs sdl2 2>/dev/null)
SDL2_TTF_CFLAGS ?= $(shell pkg-config --cflags SDL2_ttf 2>/dev/null)
SDL2_TTF_LIBS ?= $(shell pkg-config --libs SDL2_ttf 2>/dev/null)
ifeq ($(SDL2_CFLAGS),)
SDL2_CFLAGS = -I/usr/include/SDL2 -D_REENTRANT
endif
ifeq ($(SDL2_LIBS),)
SDL2_LIBS = -lSDL2
endif
ifeq ($(SDL2_TTF_CFLAGS),)
SDL2_TTF_CFLAGS =
endif
ifeq ($(SDL2_TTF_LIBS),)
SDL2_TTF_LIBS = -lSDL2_ttf
endif
CFLAGS += $(SDL2_CFLAGS) $(SDL2_TTF_CFLAGS)
LDFLAGS = -lasound -lm -pthread

# --------------------------------------------------------------------
# Design principle: Separate compilation of library sources from main sources.
# All .c files in lib/ are compiled into object files and linked with every target.
# --------------------------------------------------------------------

# Find all .c files in the lib folder (library function sources)
LIB_SRCS = $(shell find ./lib -type f -name '*.c')
LIB_OBJS = $(LIB_SRCS:.c=.o)

# Find all .c files recursively (all sources, except user folders and .git)
ALL_SOURCES = $(shell find . -type f -name '*.c' -not -path "./users/*" -not -path "*/.git/*")

# Exclude command, app, and lib sources from the main executable sources.
NON_COMMAND_SOURCES = $(filter-out ./commands/% ./apps/% ./games/% ./lib/% ./utilities/% ./$(TERMINAL_SRC_DIR)/%, $(ALL_SOURCES))
NON_COMMAND_OBJECTS = $(NON_COMMAND_SOURCES:.c=.o)
TARGET = budostack
BIN_DIR = bin
TERMINAL_SRC_DIR = terminal_src
TERMINAL_SRCS = $(shell find ./$(TERMINAL_SRC_DIR) -maxdepth 1 -type f -name '*.c')
TERMINAL_OBJS = $(TERMINAL_SRCS:.c=.o)
TERMINAL_TARGET = $(BIN_DIR)/terminal

# Find all .c files in the commands folder
COMMANDS_SRCS = $(shell find ./commands -type f -name '*.c')
COMMANDS_EXES = $(COMMANDS_SRCS:.c=)

# Find all .c files in the apps folder
APPS_SRCS = $(shell find ./apps -type f -name '*.c')
APPS_EXES = $(APPS_SRCS:.c=)

# Find all .c files in the games folder
GAMES_SRCS = $(shell find ./games -type f -name '*.c')
GAMES_EXES = $(GAMES_SRCS:.c=)

# Find all .c files in the utilities folder
UTILITIES_SRCS = $(shell find ./utilities -type f -name '*.c')
UTILITIES_EXES = $(UTILITIES_SRCS:.c=)

# Define all targets (main, commands, and apps)
ALL_TARGETS = $(TARGET) $(COMMANDS_EXES) $(APPS_EXES) $(GAMES_EXES) $(UTILITIES_EXES) $(TERMINAL_TARGET)

.PHONY: all clean

all: $(ALL_TARGETS)

# Build the main executable from non-command sources and link with lib objects
$(TARGET): $(NON_COMMAND_OBJECTS) $(LIB_OBJS)
	@echo "Linking $(TARGET)..."
	$(CC) $(NON_COMMAND_OBJECTS) $(LIB_OBJS) $(LDFLAGS) -o $(TARGET)

$(TERMINAL_TARGET): $(TERMINAL_OBJS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking $@..."
	$(CC) $(TERMINAL_OBJS) $(LDFLAGS) $(SDL2_LIBS) $(SDL2_TTF_LIBS) -lutil -o $@

# For each executable, link its corresponding object file with the lib objects.
$(COMMANDS_EXES) $(APPS_EXES) $(GAMES_EXES) $(UTILITIES_EXES): %: %.o $(LIB_OBJS)
	@echo "Linking $@..."
	$(CC) $< $(LIB_OBJS) $(LDFLAGS) -o $@

# Pattern rule: compile any .c file into its corresponding .o file.
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Clean: remove all executables and all .o files recursively.
clean:
	rm -f $(TARGET) $(COMMANDS_EXES) $(APPS_EXES) $(GAMES_EXES) $(UTILITIES_EXES) $(TERMINAL_TARGET)
	@echo "Removing all .o files..."
	find . -type f -name '*.o' -delete
