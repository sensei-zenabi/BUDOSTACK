#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct file_list {
    char **items;
    size_t count;
    size_t capacity;
};

struct block_state {
    int saw_statement;
    int is_aggregate;
};

struct scan_state {
    int in_block_comment;
    int in_string;
    int in_char;
    int escape;
};

struct string_builder {
    char *data;
    size_t length;
    size_t capacity;
};

static void print_usage(void) {
    fprintf(stderr, "usage: creview <main> <additional_files>\n");
    fprintf(stderr, "   ex: creview main.c\n");
    fprintf(stderr, "   ex: creview ./src/main.c ./src/\n");
    fprintf(stderr, "   ex: creview main.c include1.c include2.h\n");
}

static int file_list_contains(const struct file_list *list, const char *path) {
    size_t i;

    for (i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], path) == 0) {
            return 1;
        }
    }

    return 0;
}

static int file_list_add(struct file_list *list, const char *path) {
    char **items;
    char *copy;

    if (file_list_contains(list, path)) {
        return 0;
    }

    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 8 : list->capacity * 2;

        items = realloc(list->items, new_capacity * sizeof(*items));
        if (items == NULL) {
            return -1;
        }
        list->items = items;
        list->capacity = new_capacity;
    }

    copy = strdup(path);
    if (copy == NULL) {
        return -1;
    }

    list->items[list->count++] = copy;
    return 0;
}

static void file_list_free(struct file_list *list) {
    size_t i;

    for (i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
}

static void string_builder_free(struct string_builder *builder) {
    free(builder->data);
    builder->data = NULL;
    builder->length = 0;
    builder->capacity = 0;
}

static int string_builder_append(struct string_builder *builder, const char *text) {
    size_t text_len = strlen(text);
    size_t needed = builder->length + text_len + 1;
    char *next;

    if (needed > builder->capacity) {
        size_t new_capacity = builder->capacity == 0 ? 128 : builder->capacity * 2;

        while (new_capacity < needed) {
            new_capacity *= 2;
        }

        next = realloc(builder->data, new_capacity);
        if (next == NULL) {
            return -1;
        }
        builder->data = next;
        builder->capacity = new_capacity;
    }

    memcpy(builder->data + builder->length, text, text_len);
    builder->length += text_len;
    builder->data[builder->length] = '\0';
    return 0;
}

static int string_builder_append_char(struct string_builder *builder, char ch) {
    char text[2] = {ch, '\0'};

    return string_builder_append(builder, text);
}

static int string_builder_append_quoted(struct string_builder *builder, const char *text) {
    const char *cursor = text;

    if (string_builder_append_char(builder, '\'') != 0) {
        return -1;
    }

    while (*cursor != '\0') {
        if (*cursor == '\'') {
            if (string_builder_append(builder, "'\\''") != 0) {
                return -1;
            }
            cursor++;
            continue;
        }
        if (string_builder_append_char(builder, *cursor) != 0) {
            return -1;
        }
        cursor++;
    }

    return string_builder_append_char(builder, '\'');
}

static int add_search_path(struct file_list *list, const char *path) {
    return file_list_add(list, path);
}

static int has_c_suffix(const char *name) {
    size_t len = strlen(name);

    if (len < 3) {
        return 0;
    }

    return strcmp(name + len - 2, ".c") == 0 || strcmp(name + len - 2, ".h") == 0;
}

static int should_scan_include(const char *name) {
    return has_c_suffix(name);
}

static int add_directory_files(struct file_list *list, const char *path) {
    DIR *dir = opendir(path);
    struct dirent *entry;
    char full_path[1024];
    int result = 0;

    if (dir == NULL) {
        perror(path);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (!has_c_suffix(entry->d_name)) {
            continue;
        }

        if (snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name) >= (int)sizeof(full_path)) {
            fprintf(stderr, "Path too long: %s/%s\n", path, entry->d_name);
            result = -1;
            continue;
        }

        if (file_list_add(list, full_path) != 0) {
            result = -1;
        }
    }

    closedir(dir);
    return result;
}

static int add_path(struct file_list *list, const char *path) {
    struct stat st;

    if (stat(path, &st) != 0) {
        perror(path);
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        return add_directory_files(list, path);
    }

    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "Skipping non-regular file: %s\n", path);
        return 0;
    }

    return file_list_add(list, path);
}

static int get_dirname(const char *path, char *buffer, size_t buffer_size) {
    const char *slash = strrchr(path, '/');
    size_t length;

    if (slash == NULL) {
        if (snprintf(buffer, buffer_size, ".") >= (int)buffer_size) {
            return -1;
        }
        return 0;
    }

    length = (size_t)(slash - path);
    if (length == 0) {
        length = 1;
    }

    if (length + 1 > buffer_size) {
        return -1;
    }

    memcpy(buffer, path, length);
    buffer[length] = '\0';
    return 0;
}

static int file_exists(const char *path) {
    struct stat st;

    if (stat(path, &st) != 0) {
        return 0;
    }

    return S_ISREG(st.st_mode);
}

static void try_add_include(struct file_list *files, const char *base_dir, const char *include_name) {
    char candidate[1024];

    if (!should_scan_include(include_name)) {
        return;
    }

    if (snprintf(candidate, sizeof(candidate), "%s/%s", base_dir, include_name) >= (int)sizeof(candidate)) {
        return;
    }

    if (file_exists(candidate)) {
        file_list_add(files, candidate);
    }
}

static void scan_includes(const char *file,
                          const char *contents,
                          const struct file_list *search_dirs,
                          struct file_list *files) {
    char current_dir[1024];
    const char *cursor = contents;
    const char *line_start = contents;
    int line_num = 1;

    if (get_dirname(file, current_dir, sizeof(current_dir)) != 0) {
        return;
    }

    while (*cursor != '\0') {
        if (*cursor == '\n') {
            size_t len = (size_t)(cursor - line_start);
            size_t i = 0;

            while (i < len && isspace((unsigned char)line_start[i])) {
                i++;
            }

            if (i < len && line_start[i] == '#') {
                const char *include_start = NULL;
                size_t include_len = 0;
                size_t j = i + 1;

                while (j < len && isspace((unsigned char)line_start[j])) {
                    j++;
                }

                if (j + 7 < len && strncmp(line_start + j, "include", 7) == 0) {
                    j += 7;
                    while (j < len && isspace((unsigned char)line_start[j])) {
                        j++;
                    }
                    if (j < len && line_start[j] == '\"') {
                        include_start = line_start + j + 1;
                        j++;
                        while (j < len && line_start[j] != '\"') {
                            j++;
                        }
                        include_len = (size_t)(line_start + j - include_start);
                    }
                }

                if (include_start != NULL && include_len > 0) {
                    char include_name[256];
                    size_t copy_len = include_len;
                    size_t d;

                    if (copy_len >= sizeof(include_name)) {
                        copy_len = sizeof(include_name) - 1;
                    }
                    memcpy(include_name, include_start, copy_len);
                    include_name[copy_len] = '\0';

                    try_add_include(files, current_dir, include_name);
                    for (d = 0; d < search_dirs->count; d++) {
                        try_add_include(files, search_dirs->items[d], include_name);
                    }
                }
            }

            line_num++;
            line_start = cursor + 1;
        }
        cursor++;
    }
    (void)line_num;
}

static int is_word_boundary(int ch) {
    return ch == '\0' || (!isalnum((unsigned char)ch) && ch != '_');
}

static int contains_word(const char *line, const char *word) {
    size_t word_len = strlen(word);
    const char *pos = line;

    while ((pos = strstr(pos, word)) != NULL) {
        if ((pos == line || is_word_boundary(pos[-1])) && is_word_boundary(pos[word_len])) {
            return 1;
        }
        pos += word_len;
    }

    return 0;
}

static int has_for_loop_declaration(const char *line) {
    const char *pos = line;
    const char *types[] = {"int", "char", "long", "short", "float", "double", "signed", "unsigned", "size_t", NULL};
    size_t i;

    while ((pos = strstr(pos, "for")) != NULL) {
        const char *cursor = pos + 3;

        if (!(pos == line || is_word_boundary(pos[-1])) || !is_word_boundary(pos[3])) {
            pos += 3;
            continue;
        }

        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor != '(') {
            pos += 3;
            continue;
        }
        cursor++;
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        for (i = 0; types[i] != NULL; i++) {
            size_t len = strlen(types[i]);
            if (strncmp(cursor, types[i], len) == 0 && is_word_boundary(cursor[len])) {
                return 1;
            }
        }
        pos += 3;
    }

    return 0;
}

static int is_declaration_line(const char *line) {
    const char *cursor = line;
    const char *types[] = {
        "auto", "char", "const", "double", "enum", "extern", "float",
        "int", "long", "register", "short", "signed", "static", "struct",
        "typedef", "union", "unsigned", "void", "volatile", NULL
    };
    const char *control_keywords[] = {
        "return", "goto", "break", "continue", "if", "for", "while", "switch",
        "case", "default", NULL
    };
    size_t i;

    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '\0' || *cursor == '#' || *cursor == '/' || *cursor == '{' || *cursor == '}') {
        return 0;
    }
    for (i = 0; control_keywords[i] != NULL; i++) {
        size_t len = strlen(control_keywords[i]);
        if (strncmp(cursor, control_keywords[i], len) == 0 && is_word_boundary(cursor[len])) {
            return 0;
        }
    }
    for (i = 0; types[i] != NULL; i++) {
        size_t len = strlen(types[i]);
        if (strncmp(cursor, types[i], len) == 0 && is_word_boundary(cursor[len])) {
            return 1;
        }
    }

    if (isalpha((unsigned char)*cursor) || *cursor == '_') {
        while (isalnum((unsigned char)*cursor) || *cursor == '_') {
            cursor++;
        }
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        while (*cursor == '*') {
            cursor++;
            while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
                cursor++;
            }
        }
        if (isalpha((unsigned char)*cursor) || *cursor == '_') {
            return 1;
        }
    }

    return 0;
}

static int line_has_statement(const char *line) {
    const char *cursor = line;

    while (*cursor != '\0') {
        if (*cursor == ';') {
            return 1;
        }
        cursor++;
    }

    return 0;
}

static int is_constant_expression(const char *text) {
    const char *cursor = text;

    if (*text == '\0') {
        return 1;
    }

    while (*cursor != '\0') {
        if (isspace((unsigned char)*cursor)) {
            cursor++;
            continue;
        }

        if (isdigit((unsigned char)*cursor)) {
            const char *start = cursor;

            if (*cursor == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
                cursor += 2;
                while (isxdigit((unsigned char)*cursor)) {
                    cursor++;
                }
            } else {
                while (isdigit((unsigned char)*cursor)) {
                    cursor++;
                }
            }

            while (*cursor == 'u' || *cursor == 'U' || *cursor == 'l' || *cursor == 'L') {
                cursor++;
            }

            if (cursor == start) {
                return 0;
            }
            continue;
        }

        if (isupper((unsigned char)*cursor) || *cursor == '_') {
            cursor++;
            while (isupper((unsigned char)*cursor) || isdigit((unsigned char)*cursor) || *cursor == '_') {
                cursor++;
            }
            continue;
        }

        if (strncmp(cursor, "sizeof", 6) == 0 && is_word_boundary(cursor[6])) {
            cursor += 6;
            continue;
        }

        if (strchr("()+-*/%<>=!&|^~?:,", *cursor) != NULL) {
            cursor++;
            continue;
        }

        return 0;
    }

    return 1;
}

static void report_issue(const char *file, int line, const char *message, int *issue_count) {
    if (line > 0) {
        printf("%s:%d: %s\n", file, line, message);
    } else {
        printf("%s: %s\n", file, message);
    }
    (*issue_count)++;
}

static void check_header_guards(const char *file, const char *contents, int *issue_count) {
    const char *ifndef_pos = strstr(contents, "#ifndef");
    const char *define_pos = strstr(contents, "#define");

    if (ifndef_pos == NULL || define_pos == NULL || define_pos < ifndef_pos) {
        report_issue(file, 0, "Header guard missing or out of order (#ifndef/#define)", issue_count);
    }
}

static void scan_file(const char *file, const char *contents, int *issue_count) {
    struct scan_state state = {0, 0, 0, 0};
    struct block_state blocks[128];
    int block_depth = 0;
    int line_num = 1;
    const char *line_start = contents;
    const char *cursor = contents;
    int pending_aggregate = 0;

    blocks[0].saw_statement = 0;
    blocks[0].is_aggregate = 0;

    while (*cursor != '\0') {
        if (*cursor == '\n') {
            size_t len = (size_t)(cursor - line_start);
            char *code_line = malloc(len + 1);
            size_t i;
            int saw_comment_issue = 0;

            if (code_line == NULL) {
                fprintf(stderr, "Out of memory\n");
                return;
            }

            for (i = 0; i < len; i++) {
                char ch = line_start[i];
                char next = (i + 1 < len) ? line_start[i + 1] : '\0';

                if (state.in_block_comment) {
                    if (ch == '*' && next == '/') {
                        state.in_block_comment = 0;
                        code_line[i] = ' ';
                        if (i + 1 < len) {
                            code_line[i + 1] = ' ';
                            i++;
                        }
                    } else {
                        code_line[i] = ' ';
                    }
                    continue;
                }

                if (state.in_string) {
                    if (!state.escape && ch == '"') {
                        state.in_string = 0;
                    }
                    state.escape = (!state.escape && ch == '\\') ? 1 : 0;
                    code_line[i] = ' ';
                    continue;
                }

                if (state.in_char) {
                    if (!state.escape && ch == '\'') {
                        state.in_char = 0;
                    }
                    state.escape = (!state.escape && ch == '\\') ? 1 : 0;
                    code_line[i] = ' ';
                    continue;
                }

                if (ch == '/' && next == '/') {
                    if (!saw_comment_issue) {
                        report_issue(file, line_num, "C89 forbids // comments", issue_count);
                        saw_comment_issue = 1;
                    }
                    code_line[i] = ' ';
                    if (i + 1 < len) {
                        code_line[i + 1] = ' ';
                        i++;
                    }
                    while (i + 1 < len) {
                        i++;
                        code_line[i] = ' ';
                    }
                    break;
                }

                if (ch == '/' && next == '*') {
                    state.in_block_comment = 1;
                    code_line[i] = ' ';
                    if (i + 1 < len) {
                        code_line[i + 1] = ' ';
                        i++;
                    }
                    continue;
                }

                if (ch == '"') {
                    state.in_string = 1;
                    state.escape = 0;
                    code_line[i] = ' ';
                    continue;
                }

                if (ch == '\'') {
                    state.in_char = 1;
                    state.escape = 0;
                    code_line[i] = ' ';
                    continue;
                }

                code_line[i] = ch;
            }
            code_line[len] = '\0';

            if ((contains_word(code_line, "struct") || contains_word(code_line, "union") || contains_word(code_line, "enum"))
                && strchr(code_line, ';') == NULL) {
                pending_aggregate = 1;
            }

            if (contains_word(code_line, "inline") || contains_word(code_line, "restrict") || contains_word(code_line, "_Bool")) {
                report_issue(file, line_num, "C99 keyword used (inline/restrict/_Bool)", issue_count);
            }
            if (contains_word(code_line, "bool")) {
                report_issue(file, line_num, "C99 bool used (use int or enum)", issue_count);
            }
            if (has_for_loop_declaration(code_line)) {
                report_issue(file, line_num, "for-loop declares a variable (C89 forbids)", issue_count);
            }
            if (strstr(code_line, "static_cast") || strstr(code_line, "reinterpret_cast") || strstr(code_line, "const_cast") || strstr(code_line, "dynamic_cast")) {
                report_issue(file, line_num, "C++-style cast used", issue_count);
            }
            if (strstr(code_line, ",}") || strstr(code_line, ", }")) {
                report_issue(file, line_num, "Trailing comma in enum initializer", issue_count);
            }
            {
                const char *brace = strchr(code_line, '{');
                const char *dot = brace ? strchr(brace, '.') : NULL;
                if (dot != NULL) {
                    const char *ident = dot + 1;
                    if (isalpha((unsigned char)*ident) || *ident == '_') {
                        const char *eq = strchr(ident, '=');
                        if (eq != NULL) {
                            report_issue(file, line_num, "Possible designated initializer", issue_count);
                        }
                    }
                }
            }

            if (is_declaration_line(code_line)) {
                if (block_depth > 0 && !blocks[block_depth].is_aggregate && blocks[block_depth].saw_statement) {
                    report_issue(file, line_num, "Declaration after statement (mixed declarations/code)", issue_count);
                }
            } else if (line_has_statement(code_line)) {
                if (block_depth >= 0 && !blocks[block_depth].is_aggregate) {
                    blocks[block_depth].saw_statement = 1;
                }
            }

            for (i = 0; i < len; i++) {
                if (code_line[i] == '{') {
                    if (block_depth + 1 < (int)(sizeof(blocks) / sizeof(blocks[0]))) {
                        block_depth++;
                        blocks[block_depth].saw_statement = 0;
                        blocks[block_depth].is_aggregate = pending_aggregate;
                    }
                    pending_aggregate = 0;
                } else if (code_line[i] == '}') {
                    if (block_depth > 0) {
                        block_depth--;
                    }
                }
            }

            free(code_line);
            line_num++;
            line_start = cursor + 1;
        }
        cursor++;
    }

    if (cursor != line_start) {
        size_t len = (size_t)(cursor - line_start);
        char *code_line = malloc(len + 1);
        size_t i;
        int saw_comment_issue = 0;

        if (code_line == NULL) {
            fprintf(stderr, "Out of memory\n");
            return;
        }

        for (i = 0; i < len; i++) {
            char ch = line_start[i];
            char next = (i + 1 < len) ? line_start[i + 1] : '\0';

            if (state.in_block_comment) {
                if (ch == '*' && next == '/') {
                    state.in_block_comment = 0;
                    code_line[i] = ' ';
                    if (i + 1 < len) {
                        code_line[i + 1] = ' ';
                        i++;
                    }
                } else {
                    code_line[i] = ' ';
                }
                continue;
            }

            if (state.in_string) {
                if (!state.escape && ch == '"') {
                    state.in_string = 0;
                }
                state.escape = (!state.escape && ch == '\\') ? 1 : 0;
                code_line[i] = ' ';
                continue;
            }

            if (state.in_char) {
                if (!state.escape && ch == '\'') {
                    state.in_char = 0;
                }
                state.escape = (!state.escape && ch == '\\') ? 1 : 0;
                code_line[i] = ' ';
                continue;
            }

            if (ch == '/' && next == '/') {
                if (!saw_comment_issue) {
                    report_issue(file, line_num, "C89 forbids // comments", issue_count);
                    saw_comment_issue = 1;
                }
                code_line[i] = ' ';
                if (i + 1 < len) {
                    code_line[i + 1] = ' ';
                    i++;
                }
                while (i + 1 < len) {
                    i++;
                    code_line[i] = ' ';
                }
                break;
            }

            if (ch == '/' && next == '*') {
                state.in_block_comment = 1;
                code_line[i] = ' ';
                if (i + 1 < len) {
                    code_line[i + 1] = ' ';
                    i++;
                }
                continue;
            }

            if (ch == '"') {
                state.in_string = 1;
                state.escape = 0;
                code_line[i] = ' ';
                continue;
            }

            if (ch == '\'') {
                state.in_char = 1;
                state.escape = 0;
                code_line[i] = ' ';
                continue;
            }

            code_line[i] = ch;
        }
        code_line[len] = '\0';

        if ((contains_word(code_line, "struct") || contains_word(code_line, "union") || contains_word(code_line, "enum"))
            && strchr(code_line, ';') == NULL) {
            pending_aggregate = 1;
        }

        if (contains_word(code_line, "inline") || contains_word(code_line, "restrict") || contains_word(code_line, "_Bool")) {
            report_issue(file, line_num, "C99 keyword used (inline/restrict/_Bool)", issue_count);
        }
        if (contains_word(code_line, "bool")) {
            report_issue(file, line_num, "C99 bool used (use int or enum)", issue_count);
        }
        if (has_for_loop_declaration(code_line)) {
            report_issue(file, line_num, "for-loop declares a variable (C89 forbids)", issue_count);
        }
        if (strstr(code_line, "static_cast") || strstr(code_line, "reinterpret_cast") || strstr(code_line, "const_cast") || strstr(code_line, "dynamic_cast")) {
            report_issue(file, line_num, "C++-style cast used", issue_count);
        }
        if (strstr(code_line, ",}") || strstr(code_line, ", }")) {
            report_issue(file, line_num, "Trailing comma in enum initializer", issue_count);
        }
        {
            const char *brace = strchr(code_line, '{');
            const char *dot = brace ? strchr(brace, '.') : NULL;
            if (dot != NULL) {
                const char *ident = dot + 1;
                if (isalpha((unsigned char)*ident) || *ident == '_') {
                    const char *eq = strchr(ident, '=');
                    if (eq != NULL) {
                        report_issue(file, line_num, "Possible designated initializer", issue_count);
                    }
                }
            }
        }

        if (is_declaration_line(code_line)) {
            if (block_depth > 0 && !blocks[block_depth].is_aggregate && blocks[block_depth].saw_statement) {
                report_issue(file, line_num, "Declaration after statement (mixed declarations/code)", issue_count);
            }
        } else if (line_has_statement(code_line)) {
            if (block_depth >= 0 && !blocks[block_depth].is_aggregate) {
                blocks[block_depth].saw_statement = 1;
            }
        }

        for (i = 0; i < len; i++) {
            if (code_line[i] == '{') {
                if (block_depth + 1 < (int)(sizeof(blocks) / sizeof(blocks[0]))) {
                    block_depth++;
                    blocks[block_depth].saw_statement = 0;
                    blocks[block_depth].is_aggregate = pending_aggregate;
                }
                pending_aggregate = 0;
            } else if (code_line[i] == '}') {
                if (block_depth > 0) {
                    block_depth--;
                }
            }
        }

        free(code_line);
    }

    if (state.in_block_comment) {
        report_issue(file, line_num, "Unterminated block comment", issue_count);
    }

    if (strstr(contents, "#include <stdint.h>") || strstr(contents, "#include <stdbool.h>") || strstr(contents, "#include <stdatomic.h>")) {
        report_issue(file, 0, "C89 forbids stdint.h/stdbool.h/stdatomic.h", issue_count);
    }

    if (strstr(contents, "void main") != NULL) {
        report_issue(file, 0, "main should return int", issue_count);
    }

    if (strstr(contents, "main(") != NULL && strstr(contents, "int main") == NULL) {
        report_issue(file, 0, "main should be declared as int main(...)", issue_count);
    }
}

static void scan_vla(const char *file, const char *contents, int *issue_count) {
    int line_num = 1;
    const char *cursor = contents;
    const char *line_start = contents;

    while (*cursor != '\0') {
        if (*cursor == '\n') {
            size_t len = (size_t)(cursor - line_start);
            char *line_copy = malloc(len + 1);
            size_t i;

            if (line_copy == NULL) {
                fprintf(stderr, "Out of memory\n");
                return;
            }

            memcpy(line_copy, line_start, len);
            line_copy[len] = '\0';

            if (is_declaration_line(line_copy)) {
                const char *eq_pos = strchr(line_copy, '=');

                for (i = 0; i < len; i++) {
                    if (line_copy[i] == '[') {
                        if (eq_pos != NULL && &line_copy[i] > eq_pos) {
                            continue;
                        }
                        size_t j = i + 1;
                        char expression[64];
                        size_t exp_len = 0;

                        while (j < len && line_copy[j] != ']' && exp_len + 1 < sizeof(expression)) {
                            expression[exp_len++] = line_copy[j++];
                        }
                        expression[exp_len] = '\0';
                        if (j < len && line_copy[j] == ']' && !is_constant_expression(expression)) {
                            report_issue(file, line_num, "Possible variable-length array", issue_count);
                        }
                    }
                }
            }

            free(line_copy);

            line_num++;
            line_start = cursor + 1;
        }
        cursor++;
    }

    if (cursor != line_start) {
        size_t len = (size_t)(cursor - line_start);
        char *line_copy = malloc(len + 1);
        size_t i;

        if (line_copy == NULL) {
            fprintf(stderr, "Out of memory\n");
            return;
        }

        memcpy(line_copy, line_start, len);
        line_copy[len] = '\0';

        if (is_declaration_line(line_copy)) {
            const char *eq_pos = strchr(line_copy, '=');

            for (i = 0; i < len; i++) {
                if (line_copy[i] == '[') {
                    if (eq_pos != NULL && &line_copy[i] > eq_pos) {
                        continue;
                    }
                    size_t j = i + 1;
                    char expression[64];
                    size_t exp_len = 0;

                    while (j < len && line_copy[j] != ']' && exp_len + 1 < sizeof(expression)) {
                        expression[exp_len++] = line_copy[j++];
                    }
                    expression[exp_len] = '\0';
                    if (j < len && line_copy[j] == ']' && !is_constant_expression(expression)) {
                        report_issue(file, line_num, "Possible variable-length array", issue_count);
                    }
                }
            }
        }

        free(line_copy);
    }
}

static void run_syntax_check(const struct file_list *files, int *issue_count) {
    const char *cc = getenv("CC");
    struct string_builder command = {0};
    size_t i;
    int status;
    FILE *pipe;
    char buffer[256];
    int printed_header = 0;

    if (cc == NULL || *cc == '\0') {
        cc = "cc";
    }

    if (string_builder_append(&command, cc) != 0
        || string_builder_append(&command, " -std=c89 -pedantic -Wall -Wextra -fsyntax-only") != 0) {
        fprintf(stderr, "Out of memory\n");
        string_builder_free(&command);
        return;
    }

    for (i = 0; i < files->count; i++) {
        if (string_builder_append(&command, " ") != 0
            || string_builder_append_quoted(&command, files->items[i]) != 0) {
            fprintf(stderr, "Out of memory\n");
            string_builder_free(&command);
            return;
        }
    }

    if (string_builder_append(&command, " 2>&1") != 0) {
        fprintf(stderr, "Out of memory\n");
        string_builder_free(&command);
        return;
    }

    pipe = popen(command.data, "r");
    if (pipe == NULL) {
        perror("popen");
        string_builder_free(&command);
        return;
    }

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        if (!printed_header) {
            printf("\n[SYNTAX CHECK]\n");
            printed_header = 1;
        }
        fputs(buffer, stdout);
        (*issue_count)++;
    }

    status = pclose(pipe);
    if (status == -1) {
        perror("pclose");
        string_builder_free(&command);
        return;
    }

    string_builder_free(&command);
}

static int load_file(const char *path, char **contents) {
    FILE *fp = fopen(path, "rb");
    long length;
    char *buffer;

    if (fp == NULL) {
        perror(path);
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror(path);
        fclose(fp);
        return -1;
    }

    length = ftell(fp);
    if (length < 0) {
        perror(path);
        fclose(fp);
        return -1;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        perror(path);
        fclose(fp);
        return -1;
    }

    buffer = malloc((size_t)length + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Out of memory\n");
        fclose(fp);
        return -1;
    }

    if (fread(buffer, 1, (size_t)length, fp) != (size_t)length) {
        fprintf(stderr, "Failed to read %s\n", path);
        free(buffer);
        fclose(fp);
        return -1;
    }

    buffer[length] = '\0';
    fclose(fp);

    *contents = buffer;
    return 0;
}

int main(int argc, char *argv[]) {
    struct file_list list = {0};
    struct file_list search_dirs = {0};
    int i;
    int issue_count = 0;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    for (i = 1; i < argc; i++) {
        struct stat st;

        if (stat(argv[i], &st) != 0) {
            perror(argv[i]);
            file_list_free(&list);
            file_list_free(&search_dirs);
            return 1;
        }

        if (S_ISDIR(st.st_mode)) {
            if (add_search_path(&search_dirs, argv[i]) != 0) {
                file_list_free(&list);
                file_list_free(&search_dirs);
                return 1;
            }
            if (add_directory_files(&list, argv[i]) != 0) {
                file_list_free(&list);
                file_list_free(&search_dirs);
                return 1;
            }
        } else {
            char dir_buf[1024];

            if (add_path(&list, argv[i]) != 0) {
                file_list_free(&list);
                file_list_free(&search_dirs);
                return 1;
            }
            if (get_dirname(argv[i], dir_buf, sizeof(dir_buf)) == 0) {
                if (add_search_path(&search_dirs, dir_buf) != 0) {
                    file_list_free(&list);
                    file_list_free(&search_dirs);
                    return 1;
                }
            }
        }
    }

    if (list.count == 0) {
        fprintf(stderr, "No input files found.\n");
        file_list_free(&list);
        file_list_free(&search_dirs);
        return 1;
    }

    printf("C89/C90 review for %lu files\n", (unsigned long)list.count);
    printf("------------------------------------------------------------\n");

    for (i = 0; i < (int)list.count; i++) {
        char *contents = NULL;
        size_t len = strlen(list.items[i]);

        if (load_file(list.items[i], &contents) != 0) {
            issue_count++;
            continue;
        }

        printf("\n[FILE] %s\n", list.items[i]);

        if (len >= 2 && strcmp(list.items[i] + len - 2, ".h") == 0) {
            check_header_guards(list.items[i], contents, &issue_count);
        }

        scan_file(list.items[i], contents, &issue_count);
        scan_vla(list.items[i], contents, &issue_count);
        scan_includes(list.items[i], contents, &search_dirs, &list);

        free(contents);
    }

    run_syntax_check(&list, &issue_count);

    printf("\n------------------------------------------------------------\n");
    printf("Review complete. Issues found: %d\n", issue_count);

    file_list_free(&list);
    file_list_free(&search_dirs);
    return issue_count == 0 ? 0 : 2;
}
