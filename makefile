# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic
LDFLAGS = -lm -pthread

ALSA_LIBS = $(shell pkg-config --libs alsa 2>/dev/null)

ifneq ($(strip $(ALSA_LIBS)),)
LDFLAGS += $(ALSA_LIBS)
endif

SDL2_FOUND := 0
SDL2_CFLAGS :=
SDL2_LIBS :=

ifneq ($(strip $(shell pkg-config --exists sdl2 && echo yes 2>/dev/null)),)
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
SDL2_FOUND := 1
else ifneq ($(strip $(shell command -v sdl2-config 2>/dev/null)),)
SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS := $(shell sdl2-config --libs 2>/dev/null)
SDL2_FOUND := 1
endif

OPENGL_LIBS :=
OPENGL_FOUND := 0

ifneq ($(strip $(shell pkg-config --exists gl && echo yes 2>/dev/null)),)
OPENGL_LIBS := $(shell pkg-config --libs gl 2>/dev/null)
OPENGL_FOUND := 1
else ifneq ($(wildcard /usr/lib*/libGL.so*),)
OPENGL_LIBS := -lGL
OPENGL_FOUND := 1
endif

TERMINAL_USE_SDL := $(and $(SDL2_FOUND),$(OPENGL_FOUND))

ifeq ($(TERMINAL_USE_SDL),)
TERMINAL_USE_SDL := 0
endif

ifeq ($(TERMINAL_USE_SDL),1)
apps/terminal.o: CFLAGS += $(SDL2_CFLAGS)
apps/terminal: LDFLAGS += $(SDL2_LIBS) $(OPENGL_LIBS)
else
apps/terminal.o: CFLAGS += -DBUDOSTACK_HAVE_SDL2=0
endif

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
NON_COMMAND_SOURCES = $(filter-out ./commands/% ./apps/% ./games/% ./lib/% ./utilities/%, $(ALL_SOURCES))
NON_COMMAND_OBJECTS = $(NON_COMMAND_SOURCES:.c=.o)
TARGET = budostack

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
ALL_TARGETS = $(TARGET) $(COMMANDS_EXES) $(APPS_EXES) $(GAMES_EXES) $(UTILITIES_EXES)

.PHONY: all clean

all: $(ALL_TARGETS)

# Build the main executable from non-command sources and link with lib objects
$(TARGET): $(NON_COMMAND_OBJECTS) $(LIB_OBJS)
	@echo "Linking $(TARGET)..."
	$(CC) $(NON_COMMAND_OBJECTS) $(LIB_OBJS) $(LDFLAGS) -o $(TARGET)

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
	rm -f $(TARGET) $(COMMANDS_EXES) $(APPS_EXES) $(GAMES_EXES) $(UTILITIES_EXES)
	@echo "Removing all .o files..."
	$(shell find . -type f -name '*.o' -delete)
