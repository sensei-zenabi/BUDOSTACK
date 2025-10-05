#include "terminal_buffer.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TERMINAL_BUFFER_GROWTH 16

static int ensure_line_capacity(TerminalLine *line, size_t required)
{
    if (required + 1 <= line->capacity) {
        return 0;
    }

    size_t new_capacity = line->capacity ? line->capacity : 32;
    while (new_capacity < required + 1) {
        if (new_capacity > (SIZE_MAX / 2)) {
            errno = ENOMEM;
            return -1;
        }
        new_capacity *= 2;
    }

    char *new_text = realloc(line->text, new_capacity);
    if (!new_text) {
        return -1;
    }

    line->text = new_text;
    line->capacity = new_capacity;
    return 0;
}

static int init_line(TerminalLine *line)
{
    line->text = NULL;
    line->length = 0;
    line->capacity = 0;
    if (ensure_line_capacity(line, 0) == -1) {
        return -1;
    }
    if (line->text) {
        line->text[0] = '\0';
    }
    return 0;
}

static void destroy_line(TerminalLine *line)
{
    free(line->text);
    line->text = NULL;
    line->length = 0;
    line->capacity = 0;
}

static int append_char_to_line(TerminalLine *line, char c)
{
    if (ensure_line_capacity(line, line->length + 1) == -1) {
        return -1;
    }
    line->text[line->length++] = c;
    line->text[line->length] = '\0';
    return 0;
}

static int append_text_to_line(TerminalLine *line, const char *text, size_t length)
{
    if (ensure_line_capacity(line, line->length + length) == -1) {
        return -1;
    }
    memcpy(line->text + line->length, text, length);
    line->length += length;
    line->text[line->length] = '\0';
    return 0;
}

static int ensure_buffer_capacity(TerminalBuffer *buffer, size_t required)
{
    if (required <= buffer->capacity) {
        return 0;
    }

    size_t new_capacity = buffer->capacity ? buffer->capacity : TERMINAL_BUFFER_GROWTH;
    while (new_capacity < required) {
        if (new_capacity > (SIZE_MAX / 2)) {
            errno = ENOMEM;
            return -1;
        }
        new_capacity *= 2;
    }

    TerminalLine *new_lines = realloc(buffer->lines, new_capacity * sizeof(*new_lines));
    if (!new_lines) {
        return -1;
    }

    buffer->lines = new_lines;
    buffer->capacity = new_capacity;
    return 0;
}

static int push_line(TerminalBuffer *buffer)
{
    if (ensure_buffer_capacity(buffer, buffer->count + 1) == -1) {
        return -1;
    }

    TerminalLine *line = &buffer->lines[buffer->count];
    if (init_line(line) == -1) {
        return -1;
    }

    buffer->count++;
    return 0;
}

static void drop_oldest_line(TerminalBuffer *buffer)
{
    if (buffer->count == 0) {
        return;
    }

    destroy_line(&buffer->lines[0]);
    memmove(&buffer->lines[0], &buffer->lines[1], (buffer->count - 1) * sizeof(TerminalLine));
    buffer->count--;
}

int terminal_buffer_init(TerminalBuffer *buffer, size_t max_lines)
{
    if (!buffer || max_lines == 0) {
        errno = EINVAL;
        return -1;
    }

    buffer->lines = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
    buffer->max_lines = max_lines;
    buffer->pending_carriage_return = 0;

    if (push_line(buffer) == -1) {
        terminal_buffer_destroy(buffer);
        return -1;
    }

    return 0;
}

void terminal_buffer_destroy(TerminalBuffer *buffer)
{
    if (!buffer) {
        return;
    }

    for (size_t i = 0; i < buffer->count; ++i) {
        destroy_line(&buffer->lines[i]);
    }
    free(buffer->lines);
    buffer->lines = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
    buffer->max_lines = 0;
    buffer->pending_carriage_return = 0;
}

static int new_line(TerminalBuffer *buffer)
{
    if (push_line(buffer) == -1) {
        return -1;
    }

    if (buffer->count > buffer->max_lines) {
        drop_oldest_line(buffer);
    }

    return 0;
}

static int append_tab(TerminalLine *line)
{
    const char spaces[4] = {' ', ' ', ' ', ' '};
    return append_text_to_line(line, spaces, sizeof(spaces));
}

int terminal_buffer_append(TerminalBuffer *buffer, const char *data, size_t length)
{
    if (!buffer || !data) {
        errno = EINVAL;
        return -1;
    }

    if (buffer->count == 0) {
        if (push_line(buffer) == -1) {
            return -1;
        }
    }

    for (size_t i = 0; i < length; ++i) {
        char c = data[i];
        TerminalLine *line = &buffer->lines[buffer->count - 1];

        if (buffer->pending_carriage_return) {
            if (c == '\n') {
                buffer->pending_carriage_return = 0;
                if (new_line(buffer) == -1) {
                    return -1;
                }
                continue;
            }

            buffer->pending_carriage_return = 0;
            line->length = 0;
            if (line->text) {
                line->text[0] = '\0';
            }
        }

        switch (c) {
        case '\r':
            buffer->pending_carriage_return = 1;
            break;
        case '\n':
            if (new_line(buffer) == -1) {
                return -1;
            }
            break;
        case '\t':
            if (append_tab(line) == -1) {
                return -1;
            }
            break;
        default:
            if ((unsigned char)c < 0x20 && c != '\b') {
                break;
            }
            if (c == '\b') {
                if (line->length > 0) {
                    line->length--;
                    line->text[line->length] = '\0';
                }
                break;
            }
            if (append_char_to_line(line, c) == -1) {
                return -1;
            }
            break;
        }
    }

    return 0;
}

const char *terminal_buffer_get_line(const TerminalBuffer *buffer, size_t index)
{
    if (!buffer || index >= buffer->count) {
        return NULL;
    }
    return buffer->lines[index].text ? buffer->lines[index].text : "";
}

size_t terminal_buffer_line_count(const TerminalBuffer *buffer)
{
    if (!buffer) {
        return 0;
    }
    return buffer->count;
}
