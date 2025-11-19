#include "crt_shader_stack.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *crt_shader_read_text_file(const char *path, size_t *out_size) {
    if (!path) {
        return NULL;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    size_t size = (size_t)file_size;
    char *buffer = malloc(size + 1u);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, fp);
    fclose(fp);
    if (read_bytes != size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (out_size) {
        *out_size = size;
    }
    return buffer;
}

const char *crt_shader_skip_utf8_bom(const char *src, size_t *size) {
    if (!src || !size) {
        return src;
    }
    if (*size >= 3u) {
        const unsigned char *bytes = (const unsigned char *)src;
        if (bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu) {
            *size -= 3u;
            return src + 3;
        }
    }
    return src;
}

const char *crt_shader_skip_leading_space_and_comments(const char *src, const char *end) {
    const char *ptr = src;
    while (ptr < end) {
        while (ptr < end && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        if ((end - ptr) >= 2 && ptr[0] == '/' && ptr[1] == '/') {
            ptr += 2;
            while (ptr < end && *ptr != '\n') {
                ptr++;
            }
            continue;
        }
        if ((end - ptr) >= 2 && ptr[0] == '/' && ptr[1] == '*') {
            ptr += 2;
            while ((end - ptr) >= 2 && !(ptr[0] == '*' && ptr[1] == '/')) {
                ptr++;
            }
            if ((end - ptr) >= 2) {
                ptr += 2;
            }
            continue;
        }
        break;
    }
    return ptr;
}

void crt_shader_free_parameters(struct crt_shader_parameter *params, size_t count) {
    if (!params) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(params[i].name);
    }
    free(params);
}

int crt_shader_parse_parameters(const char *source,
                                size_t length,
                                struct crt_shader_parameter **out_params,
                                size_t *out_count) {
    if (!out_params || !out_count) {
        return -1;
    }
    *out_params = NULL;
    *out_count = 0u;
    if (!source || length == 0u) {
        return 0;
    }

    struct crt_shader_parameter *params = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    const char *ptr = source;
    const char *end = source + length;

    while (ptr < end) {
        const char *line_start = ptr;
        const char *line_end = line_start;
        while (line_end < end && line_end[0] != '\n' && line_end[0] != '\r') {
            line_end++;
        }

        const char *cursor = line_start;
        while (cursor < line_end && (*cursor == ' ' || *cursor == '\t')) {
            cursor++;
        }

        if ((size_t)(line_end - cursor) >= 7u && strncmp(cursor, "#pragma", 7) == 0) {
            cursor += 7;
            while (cursor < line_end && isspace((unsigned char)*cursor)) {
                cursor++;
            }

            const char keyword[] = "parameter";
            size_t keyword_len = sizeof(keyword) - 1u;
            if ((size_t)(line_end - cursor) >= keyword_len && strncmp(cursor, keyword, keyword_len) == 0) {
                const char *after_keyword = cursor + keyword_len;
                if (after_keyword < line_end && !isspace((unsigned char)*after_keyword)) {
                    /* Likely parameteri or another pragma, ignore. */
                } else {
                    cursor = after_keyword;
                    while (cursor < line_end && isspace((unsigned char)*cursor)) {
                        cursor++;
                    }

                    const char *name_start = cursor;
                    while (cursor < line_end && (isalnum((unsigned char)*cursor) || *cursor == '_')) {
                        cursor++;
                    }
                    const char *name_end = cursor;
                    if (name_end > name_start) {
                        size_t name_len = (size_t)(name_end - name_start);
                        while (cursor < line_end && isspace((unsigned char)*cursor)) {
                            cursor++;
                        }
                        if (cursor < line_end && *cursor == '"') {
                            cursor++;
                            while (cursor < line_end && *cursor != '"') {
                                cursor++;
                            }
                            if (cursor < line_end && *cursor == '"') {
                                cursor++;
                                while (cursor < line_end && isspace((unsigned char)*cursor)) {
                                    cursor++;
                                }
                                if (cursor < line_end) {
                                    const char *value_start = cursor;
                                    while (cursor < line_end && !isspace((unsigned char)*cursor)) {
                                        cursor++;
                                    }
                                    size_t value_len = (size_t)(cursor - value_start);
                                    if (value_len > 0u) {
                                        char stack_buffer[64];
                                        char *value_str = stack_buffer;
                                        char *heap_buffer = NULL;
                                        if (value_len >= sizeof(stack_buffer)) {
                                            heap_buffer = malloc(value_len + 1u);
                                            if (!heap_buffer) {
                                                crt_shader_free_parameters(params, count);
                                                return -1;
                                            }
                                            value_str = heap_buffer;
                                        }
                                        memcpy(value_str, value_start, value_len);
                                        value_str[value_len] = '\0';

                                        errno = 0;
                                        char *endptr = NULL;
                                        double parsed = strtod(value_str, &endptr);
                                        if (endptr != value_str && errno != ERANGE) {
                                            char *name_copy = malloc(name_len + 1u);
                                            if (!name_copy) {
                                                free(heap_buffer);
                                                crt_shader_free_parameters(params, count);
                                                return -1;
                                            }
                                            memcpy(name_copy, name_start, name_len);
                                            name_copy[name_len] = '\0';

                                            if (count == capacity) {
                                                size_t new_capacity = capacity == 0u ? 4u : capacity * 2u;
                                                struct crt_shader_parameter *new_params = realloc(params, new_capacity * sizeof(*new_params));
                                                if (!new_params) {
                                                    free(name_copy);
                                                    free(heap_buffer);
                                                    crt_shader_free_parameters(params, count);
                                                    return -1;
                                                }
                                                params = new_params;
                                                capacity = new_capacity;
                                            }

                                            params[count].name = name_copy;
                                            params[count].default_value = (float)parsed;
                                            count++;
                                        }
                                        free(heap_buffer);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ptr = line_end;
        while (ptr < end && (*ptr == '\n' || *ptr == '\r')) {
            ptr++;
        }
    }

    if (count == 0u) {
        free(params);
        params = NULL;
    }

    *out_params = params;
    *out_count = count;
    return 0;
}

float crt_shader_get_parameter_default(const struct crt_shader_parameter *params,
                                       size_t count,
                                       const char *name,
                                       float fallback) {
    if (!params || !name) {
        return fallback;
    }
    for (size_t i = 0; i < count; i++) {
        if (params[i].name && strcmp(params[i].name, name) == 0) {
            return params[i].default_value;
        }
    }
    return fallback;
}
