# Compiler and flags
CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic
CXXFLAGS = -std=c++11 -Wall -Wextra -Werror -Wpedantic
LDFLAGS = -lm -pthread

# Detect optional ALSA development files so ALSA-dependent tools can be built
# when the system provides them while keeping the build warning/error free when
# it does not.
ALSA_CFLAGS ?= $(shell pkg-config --cflags alsa 2>/dev/null)
ALSA_LIBS ?= $(shell pkg-config --libs alsa 2>/dev/null)

ifeq ($(strip $(ALSA_LIBS)),)
ALSA_FILES_DETECTED := $(wildcard /usr/lib*/libasound.so*)
ifneq ($(strip $(ALSA_FILES_DETECTED)),)
ALSA_LIBS = -lasound
endif
endif

ifeq ($(strip $(ALSA_LIBS)),)
CFLAGS += -DBUDOSTACK_HAVE_ALSA=0
else
CFLAGS += $(ALSA_CFLAGS) -DBUDOSTACK_HAVE_ALSA=1
LDFLAGS += $(ALSA_LIBS)
endif

SDL2_CFLAGS = $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS = $(shell pkg-config --libs sdl2 2>/dev/null)
SDL2_ENABLED = 1

ifeq ($(strip $(SDL2_LIBS)),)
SDL2_CFLAGS = $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS = $(shell sdl2-config --libs 2>/dev/null)
endif

ifeq ($(strip $(SDL2_LIBS)),)
SDL2_INCLUDE_DIR := $(wildcard /usr/include/SDL2)
ifneq ($(strip $(SDL2_INCLUDE_DIR)),)
SDL2_CFLAGS = -I$(SDL2_INCLUDE_DIR)
endif
SDL2_LIB_FILES := $(wildcard /usr/lib*/libSDL2.so*)
ifneq ($(strip $(SDL2_LIB_FILES)),)
SDL2_LIBS = -lSDL2
endif
endif

SDL2_GL_LIBS = $(shell pkg-config --libs gl 2>/dev/null)
ifeq ($(strip $(SDL2_GL_LIBS)),)
SDL2_GL_FILES := $(wildcard /usr/lib*/libGL.so*)
ifneq ($(strip $(SDL2_GL_FILES)),)
SDL2_GL_LIBS = -lGL
endif
endif

ifeq ($(strip $(SDL2_LIBS)),)
SDL2_ENABLED = 0
endif

ifeq ($(strip $(SDL2_GL_LIBS)),)
SDL2_ENABLED = 0
endif

ifeq ($(SDL2_ENABLED),1)
apps/terminal.o: CFLAGS += $(SDL2_CFLAGS)
apps/terminal: LDFLAGS += $(SDL2_LIBS) $(SDL2_GL_LIBS)
apps/dosbox_pure.o: CXXFLAGS += $(SDL2_CFLAGS)
apps/dosbox_pure: LDFLAGS += $(SDL2_LIBS) $(SDL2_GL_LIBS) -ldl
endif

# --------------------------------------------------------------------
# Design principle: Separate compilation of library sources from main sources.
# All .c files in lib/ are compiled into object files and linked with every target.
# --------------------------------------------------------------------

# Find all .c files in the lib folder (library function sources)
LIB_SRCS = $(shell find ./lib -type f -name '*.c')
LIB_OBJS = $(LIB_SRCS:.c=.o)
ifeq ($(SDL2_ENABLED),0)
LIB_SRCS := $(filter-out ./lib/retro_shader_bridge.c, $(LIB_SRCS))
LIB_OBJS := $(LIB_SRCS:.c=.o)
endif

# Find all .c files recursively (all sources, except user folders and .git)
ALL_SOURCES = $(shell find . -type f -name '*.c' -not -path "./users/*" -not -path "*/.git/*")

# Exclude command, app, and lib sources from the main executable sources.
NON_COMMAND_SOURCES = $(filter-out ./commands/% ./apps/% ./games/% ./lib/% ./utilities/%, $(ALL_SOURCES))
NON_COMMAND_OBJECTS = $(NON_COMMAND_SOURCES:.c=.o)
TARGET = budostack

# Find all .c files in the commands folder
COMMANDS_SRCS = $(shell find ./commands -type f -name '*.c')
COMMANDS_CPP_SRCS = $(shell find ./commands -type f -name '*.cpp')
COMMANDS_EXES = $(COMMANDS_SRCS:.c=) $(COMMANDS_CPP_SRCS:.cpp=)

# Find all .c files in the apps folder
APPS_SRCS = $(shell find ./apps -type f -name '*.c')
APPS_CPP_SRCS = $(shell find ./apps -type f -name '*.cpp')
APPS_EXES = $(APPS_SRCS:.c=) $(APPS_CPP_SRCS:.cpp=)

ifeq ($(SDL2_ENABLED),0)
APPS_SRCS := $(filter-out ./apps/terminal.c, $(APPS_SRCS))
APPS_EXES := $(filter-out ./apps/terminal, $(APPS_EXES))
APPS_CPP_SRCS := $(filter-out ./apps/dosbox_pure.cpp, $(APPS_CPP_SRCS))
APPS_EXES := $(filter-out ./apps/dosbox_pure, $(APPS_EXES))
endif

# Find all .c files in the games folder
GAMES_SRCS = $(shell find ./games -type f -name '*.c')
GAMES_CPP_SRCS = $(shell find ./games -type f -name '*.cpp')
GAMES_EXES = $(GAMES_SRCS:.c=) $(GAMES_CPP_SRCS:.cpp=)

# Find all .c files in the utilities folder
UTILITIES_SRCS = $(shell find ./utilities -type f -name '*.c')
UTILITIES_CPP_SRCS = $(shell find ./utilities -type f -name '*.cpp')
UTILITIES_EXES = $(UTILITIES_SRCS:.c=) $(UTILITIES_CPP_SRCS:.cpp=)

CPP_EXES = $(COMMANDS_CPP_SRCS:.cpp=) $(APPS_CPP_SRCS:.cpp=) $(GAMES_CPP_SRCS:.cpp=) $(UTILITIES_CPP_SRCS:.cpp=)

DOSBOX_PURE_CORE_DIR ?= ./cores/dosbox_pure
ifneq ($(wildcard $(DOSBOX_PURE_CORE_DIR)),)
DOSBOX_PURE_CORE_C_SRCS := $(shell find $(DOSBOX_PURE_CORE_DIR) -type f -name '*.c')
DOSBOX_PURE_CORE_CPP_SRCS := $(shell find $(DOSBOX_PURE_CORE_DIR) -type f -name '*.cpp')
DOSBOX_PURE_CORE_OBJS := $(DOSBOX_PURE_CORE_C_SRCS:.c=.o) $(DOSBOX_PURE_CORE_CPP_SRCS:.cpp=.o)
DOSBOX_PURE_CORE_LIB := $(DOSBOX_PURE_CORE_DIR)/libdosbox_pure.a
endif

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
	$(if $(filter $@,$(CPP_EXES)),$(CXX),$(CC)) $< $(LIB_OBJS) $(LDFLAGS) -o $@

# Pattern rule: compile any .c file into its corresponding .o file.
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

ifneq ($(strip $(DOSBOX_PURE_CORE_OBJS)),)
$(DOSBOX_PURE_CORE_LIB): $(DOSBOX_PURE_CORE_OBJS)
	@echo "Archiving dosbox-pure core..."
	ar rcs $(DOSBOX_PURE_CORE_LIB) $(DOSBOX_PURE_CORE_OBJS)

apps/dosbox_pure: $(DOSBOX_PURE_CORE_LIB)
apps/dosbox_pure: LDFLAGS += $(DOSBOX_PURE_CORE_LIB)
endif

# Clean: remove all executables and all .o files recursively.
clean:
	rm -f $(TARGET) $(COMMANDS_EXES) $(APPS_EXES) $(GAMES_EXES) $(UTILITIES_EXES)
	@echo "Removing all .o files..."
	$(shell find . -type f -name '*.o' -delete)
