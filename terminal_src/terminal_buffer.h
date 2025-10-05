#ifndef TERMINAL_BUFFER_H
#define TERMINAL_BUFFER_H

#include <stddef.h>

typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} TerminalLine;

typedef struct {
    TerminalLine *lines;
    size_t count;
    size_t capacity;
    size_t max_lines;
} TerminalBuffer;

int terminal_buffer_init(TerminalBuffer *buffer, size_t max_lines);
void terminal_buffer_destroy(TerminalBuffer *buffer);
int terminal_buffer_append(TerminalBuffer *buffer, const char *data, size_t length);
const char *terminal_buffer_get_line(const TerminalBuffer *buffer, size_t index);
size_t terminal_buffer_line_count(const TerminalBuffer *buffer);

#endif /* TERMINAL_BUFFER_H */
