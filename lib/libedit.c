/*
 * highlight_c_line (extended industry standard C highlighting)
 *
 * This version extends the syntax highlighter to provide industry standard
 * highlighting for C source code. It highlights:
 *   - C keywords: control keywords in blue and data type keywords in cyan.
 *   - Numeric literals in yellow.
 *   - String and character literals in green.
 *   - Single-line comments (//) in gray.
 *   - Block comments (starting with slash-star and ending with star-slash) in gray.
 *   - Parentheses in magenta.
 *
 * Design principles:
 *  - Plain C using -std=c11 and only standard libraries.
 *  - No separate header files.
 *  - Inline comments document the design choices.
 *  - Block comments spanning multiple lines are supported via a static state flag.
 *    If a block comment is started but not ended on a given line, the remaining
 *    part of that line is highlighted as a comment and the state is maintained
 *    until a star-slash is encountered.
 *  - Nested block comments are not supported; any additional slash-star inside
 *    an active block comment is treated as comment text.
 *
 * The caller is responsible for freeing the returned string.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *highlight_c_line(const char *line) {
    /* Static flag to track if we are inside a block comment spanning lines.
       When set to a nonzero value, we are inside a block comment.
       This flag is used only by this function and assumes sequential processing. */
    static int in_block_comment = 0;
    size_t len = strlen(line);
    /* Allocate extra space for ANSI escape sequences */
    size_t buf_size = len * 5 + 1;
    char *result = malloc(buf_size);
    if (!result)
        return NULL;
    size_t ri = 0;  // result index
    size_t i = 0;   // current index in the input line

    const char *comment_color = "\x1b[90m";  // gray
    const char *reset = "\x1b[0m";

    /* If the previous line ended inside a block comment, begin by highlighting
       the entire line as comment text until a closing star-slash is found. */
    if (in_block_comment) {
        size_t cl = strlen(comment_color);
        if (ri + cl < buf_size) {
            memcpy(result + ri, comment_color, cl);
            ri += cl;
        }
        while (i < len) {
            if (line[i] == '*' && i + 1 < len && line[i + 1] == '/') {
                result[ri++] = line[i++];
                result[ri++] = line[i++];
                in_block_comment = 0;
                size_t rl = strlen(reset);
                if (ri + rl < buf_size) {
                    memcpy(result + ri, reset, rl);
                    ri += rl;
                }
                break;
            } else {
                result[ri++] = line[i++];
            }
        }
        if (in_block_comment) {
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            result[ri] = '\0';
            return result;
        }
    }

    /* Process the remainder of the line normally */
    for (; i < len; i++) {
        char c = line[i];

        /* Check for single-line comment start (//) */
        if (c == '/' && i + 1 < len && line[i + 1] == '/') {
            size_t cl = strlen(comment_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, comment_color, cl);
                ri += cl;
            }
            while (i < len)
                result[ri++] = line[i++];
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            break;
        }

        /* Check for block comment start (slash-star) */
        if (c == '/' && i + 1 < len && line[i + 1] == '*') {
            size_t cl = strlen(comment_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, comment_color, cl);
                ri += cl;
            }
            result[ri++] = line[i++];  // copy slash
            result[ri++] = line[i++];  // copy star
            in_block_comment = 1;
            /* Process comment content until closing star-slash is found or end-of-line */
            while (i < len) {
                if (line[i] == '*' && i + 1 < len && line[i + 1] == '/') {
                    result[ri++] = line[i++];
                    result[ri++] = line[i++];
                    in_block_comment = 0;
                    size_t rl = strlen(reset);
                    if (ri + rl < buf_size) {
                        memcpy(result + ri, reset, rl);
                        ri += rl;
                    }
                    break;
                } else {
                    result[ri++] = line[i++];
                }
            }
            if (in_block_comment) {
                size_t rl = strlen(reset);
                if (ri + rl < buf_size) {
                    memcpy(result + ri, reset, rl);
                    ri += rl;
                }
                break;
            }
            i--;  // Adjust for loop increment
            continue;
        }

        /* Check for string literal start */
        if (c == '"') {
            const char *str_color = "\x1b[32m";  // green
            size_t cl = strlen(str_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, str_color, cl);
                ri += cl;
            }
            result[ri++] = c;  // copy starting quote
            i++;
            while (i < len) {
                result[ri++] = line[i];
                /* End string literal when an unescaped quote is found */
                if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
                    i++;
                    break;
                }
                i++;
            }
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            i--;
            continue;
        }

        /* Check for character literal start */
        if (c == '\'') {
            const char *char_color = "\x1b[32m";  // green
            size_t cl = strlen(char_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, char_color, cl);
                ri += cl;
            }
            result[ri++] = c;
            i++;
            while (i < len) {
                result[ri++] = line[i];
                if (line[i] == '\'' && (i == 0 || line[i - 1] != '\\')) {
                    i++;
                    break;
                }
                i++;
            }
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            i--;
            continue;
        }

        /* Check for identifier (potential keyword) */
        if ((c == '_' || isalpha((unsigned char)c)) &&
            ((i == 0) || (!isalnum((unsigned char)line[i - 1]) && line[i - 1] != '_'))) {
            size_t j = i;
            while (j < len && (line[j] == '_' || isalnum((unsigned char)line[j])))
                j++;
            size_t word_len = j - i;
            char word[64];
            if (word_len < sizeof(word)) {
                memcpy(word, line + i, word_len);
                word[word_len] = '\0';
            } else {
                word[0] = '\0';
            }
            static const char *keywords[] = {
                "auto", "break", "case", "const", "continue", "default",
                "do", "else", "enum", "extern", "for", "goto", "if",
                "inline", "register", "restrict", "return", "sizeof",
                "static", "struct", "switch", "typedef", "union", "volatile",
                "while", "_Alignas", "_Alignof", "_Atomic", "_Bool",
                "_Complex", "_Generic", "_Imaginary", "_Noreturn",
                "_Static_assert", "_Thread_local",
                "int", "char", "float", "double", "long", "short",
                "signed", "unsigned", "void"
            };
            size_t num_keywords = sizeof(keywords) / sizeof(keywords[0]);
            int is_keyword = 0;
            for (size_t k = 0; k < num_keywords; k++) {
                if (strcmp(word, keywords[k]) == 0) {
                    is_keyword = 1;
                    break;
                }
            }
            if (is_keyword) {
                int is_data_type = 0;
                static const char *data_types[] = {
                    "int", "char", "float", "double", "long", "short",
                    "signed", "unsigned", "void"
                };
                size_t num_data_types = sizeof(data_types) / sizeof(data_types[0]);
                for (size_t k = 0; k < num_data_types; k++) {
                    if (strcmp(word, data_types[k]) == 0) {
                        is_data_type = 1;
                        break;
                    }
                }
                const char *color = is_data_type ? "\x1b[36m" : "\x1b[34m";
                size_t cl = strlen(color);
                if (ri + cl < buf_size) {
                    memcpy(result + ri, color, cl);
                    ri += cl;
                }
                for (size_t k = i; k < j; k++) {
                    result[ri++] = line[k];
                }
                size_t rl = strlen(reset);
                if (ri + rl < buf_size) {
                    memcpy(result + ri, reset, rl);
                    ri += rl;
                }
                i = j - 1;
                continue;
            }
        }

        /* Check for numeric literals */
        if (isdigit((unsigned char)c)) {
            const char *num_color = "\x1b[33m";  // yellow
            size_t cl = strlen(num_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, num_color, cl);
                ri += cl;
            }
            while (i < len && isdigit((unsigned char)line[i])) {
                result[ri++] = line[i++];
            }
            if (i < len && line[i] == '.') {
                result[ri++] = line[i++];
                while (i < len && isdigit((unsigned char)line[i])) {
                    result[ri++] = line[i++];
                }
            }
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            i--;
            continue;
        }

        /* Check for parentheses */
        if (c == '(' || c == ')') {
            const char *paren_color = "\x1b[35m";  // magenta
            size_t cl = strlen(paren_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, paren_color, cl);
                ri += cl;
            }
            result[ri++] = c;
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            continue;
        }

        /* Default: copy character unchanged */
        result[ri++] = c;
    }
    result[ri] = '\0';
    return result;
}

/*
 * highlight_other_line (general text and markup language highlighting)
 *
 * This version provides basic syntax highlighting for general text processing
 * and markup languages. It highlights:
 *   - Markdown headers: if the first non-space character is '#' the entire line is colored red.
 *   - Markup tags: any content between '<' and '>' is colored blue.
 *
 * Design principles:
 *  - Plain C using -std=c11 and only standard libraries.
 *  - No separate header files.
 *  - The function processes the input line character by character and applies ANSI
 *    escape sequences for coloring. It handles two cases: whole-line markdown headers,
 *    and inline markup tags.
 *
 * The caller is responsible for freeing the returned string.
 */
char *highlight_other_line(const char *line) {
    size_t len = strlen(line);
    size_t buf_size = len * 5 + 1;
    char *result = malloc(buf_size);
    if (!result)
        return NULL;
    size_t ri = 0;
    size_t i = 0;

    /* Copy any leading whitespace */
    while (i < len && isspace((unsigned char)line[i])) {
        result[ri++] = line[i++];
    }
    /* Check for Markdown header (line starting with '#') */
    if (i < len && line[i] == '#') {
        const char *header_color = "\x1b[31m";  // red
        size_t cl = strlen(header_color);
        if (ri + cl < buf_size) {
            memcpy(result + ri, header_color, cl);
            ri += cl;
        }
        while (i < len)
            result[ri++] = line[i++];
        const char *reset = "\x1b[0m";
        size_t rl = strlen(reset);
        if (ri + rl < buf_size) {
            memcpy(result + ri, reset, rl);
            ri += rl;
        }
    } else {
        /* Process inline markup tags */
        int in_tag = 0;
        for (; i < len; i++) {
            char c = line[i];
            if (c == '<') {
                const char *tag_color = "\x1b[34m";
                size_t cl = strlen(tag_color);
                if (ri + cl < buf_size) {
                    memcpy(result + ri, tag_color, cl);
                    ri += cl;
                }
                in_tag = 1;
            }
            result[ri++] = c;
            if (c == '>' && in_tag) {
                const char *reset = "\x1b[0m";
                size_t rl = strlen(reset);
                if (ri + rl < buf_size) {
                    memcpy(result + ri, reset, rl);
                    ri += rl;
                }
                in_tag = 0;
            }
        }
    }
    result[ri] = '\0';
    return result;
}
