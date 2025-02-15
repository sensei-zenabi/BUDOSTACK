CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -Wno-format-truncation
LDFLAGS =
SOURCES = main.c commandparser.c input.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = terminal

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)
