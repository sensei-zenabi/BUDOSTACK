# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic
LDFLAGS = -lm -pthread

ALSA_AVAILABLE := $(shell pkg-config --exists alsa >/dev/null 2>&1 && echo 1 || echo 0)
ifeq ($(ALSA_AVAILABLE),1)
ALSA_CFLAGS := $(shell pkg-config --cflags alsa 2>/dev/null)
ALSA_LIBS := $(shell pkg-config --libs alsa 2>/dev/null)
else
ALSA_CFLAGS :=
ALSA_LIBS :=
endif

ifeq ($(ALSA_AVAILABLE),1)
CFLAGS += $(ALSA_CFLAGS) -DBUDOSTACK_HAVE_ALSA=1
LDFLAGS += $(ALSA_LIBS)
else
CFLAGS += -DBUDOSTACK_HAVE_ALSA=0
endif

SDL2_AVAILABLE := $(shell pkg-config --exists sdl2 >/dev/null 2>&1 && echo 1 || echo 0)
ifeq ($(SDL2_AVAILABLE),1)
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
else
SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS := $(shell sdl2-config --libs 2>/dev/null)
ifeq ($(strip $(SDL2_CFLAGS)$(SDL2_LIBS)),)
SDL2_AVAILABLE := 0
else
SDL2_AVAILABLE := 1
endif
endif

ifeq ($(SDL2_AVAILABLE),1)
# SDL2-specific flags are only needed for the SDL terminal target when SDL2 is available.
apps/terminal.o: CFLAGS += $(SDL2_CFLAGS) -DBUDOSTACK_HAVE_SDL2=1
apps/terminal: LDFLAGS += $(SDL2_LIBS) -lGL
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
