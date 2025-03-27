/*
 * highlight_c_line (extended industry standard C highlighting)
 *
 * This version extends the syntax highlighter to provide industry standard
 * highlighting for C source code. It highlights:
 *   - C keywords: control keywords in blue and data type keywords in cyan.
 *   - Numeric literals in yellow.
 *   - String and character literals in green.
 *   - Single-line comments (//) in gray.
 *   - Parentheses in magenta.
 *
 * Design principles:
 *  - Plain C using -std=c11 and only standard libraries.
 *  - No separate header files.
 *  - Inline comments document the design choices.
 *
 * The caller is responsible for freeing the returned string.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *highlight_line(const char *line) {
    size_t len = strlen(line);
    /* Allocate a generous buffer to hold extra escape codes. */
    size_t buf_size = len * 5 + 1;
    char *result = malloc(buf_size);
    if (!result)
        return NULL;
    size_t ri = 0;  // result buffer index

    for (size_t i = 0; i < len; i++) {
        char c = line[i];

        /* --- Check for start of single-line comment ("//") --- */
        if (c == '/' && i + 1 < len && line[i + 1] == '/') {
            const char *comment_color = "\x1b[90m";  // gray
            size_t cl = strlen(comment_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, comment_color, cl);
                ri += cl;
            }
            /* Copy rest of the line as comment */
            while (i < len) {
                result[ri++] = line[i++];
            }
            const char *reset = "\x1b[0m";
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            break;
        }

        /* --- Check for string literal start --- */
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
                /* End string literal if an unescaped quote is found */
                if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
                    i++;
                    break;
                }
                i++;
            }
            const char *reset = "\x1b[0m";
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            i--;  // adjust index for loop increment
            continue;
        }

        /* --- Check for character literal start --- */
        if (c == '\'') {
            const char *char_color = "\x1b[32m";  // green
            size_t cl = strlen(char_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, char_color, cl);
                ri += cl;
            }
            result[ri++] = c;  // copy starting single quote
            i++;
            while (i < len) {
                result[ri++] = line[i];
                if (line[i] == '\'' && (i == 0 || line[i - 1] != '\\')) {
                    i++;
                    break;
                }
                i++;
            }
            const char *reset = "\x1b[0m";
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            i--;
            continue;
        }

        /* --- Check for identifier (potential keyword) --- */
        if ((c == '_' || isalpha((unsigned char)c)) &&
            ((i == 0) || ((i > 0) && (!isalnum((unsigned char)line[i - 1]) && line[i - 1] != '_')))) {
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
            /* List of C keywords (both control and type-related) */
            static const char *keywords[] = {
                "auto", "break", "case", "const", "continue", "default",
                "do", "else", "enum", "extern", "for", "goto", "if",
                "inline", "register", "restrict", "return", "sizeof",
                "static", "struct", "switch", "typedef", "union", "volatile",
                "while", "_Alignas", "_Alignof", "_Atomic", "_Bool",
                "_Complex", "_Generic", "_Imaginary", "_Noreturn",
                "_Static_assert", "_Thread_local",
                /* Data type keywords */
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
                /* Determine if the keyword is a data type */
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
                const char *color;
                if (is_data_type) {
                    color = "\x1b[36m";  // cyan for data types
                } else {
                    color = "\x1b[34m";  // blue for other keywords
                }
                size_t cl = strlen(color);
                if (ri + cl < buf_size) {
                    memcpy(result + ri, color, cl);
                    ri += cl;
                }
                for (size_t k = i; k < j; k++) {
                    result[ri++] = line[k];
                }
                const char *reset = "\x1b[0m";
                size_t rl = strlen(reset);
                if (ri + rl < buf_size) {
                    memcpy(result + ri, reset, rl);
                    ri += rl;
                }
                i = j - 1;
                continue;
            }
        }

        /* --- Check for numeric literals --- */
        if (isdigit((unsigned char)c)) {
            const char *num_color = "\x1b[33m";  // yellow
            size_t cl = strlen(num_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, num_color, cl);
                ri += cl;
            }
            /* Copy digits (integer part) */
            while (i < len && isdigit((unsigned char)line[i])) {
                result[ri++] = line[i++];
            }
            /* Handle decimal part if present */
            if (i < len && line[i] == '.') {
                result[ri++] = line[i++];
                while (i < len && isdigit((unsigned char)line[i])) {
                    result[ri++] = line[i++];
                }
            }
            const char *reset = "\x1b[0m";
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            i--;  // adjust for the loop increment
            continue;
        }

        /* --- Check for parentheses --- */
        if (c == '(' || c == ')') {
            const char *paren_color = "\x1b[35m";  // magenta
            size_t cl = strlen(paren_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, paren_color, cl);
                ri += cl;
            }
            result[ri++] = c;
            const char *reset = "\x1b[0m";
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
            continue;
        }

        /* --- Default: copy the character unchanged --- */
        result[ri++] = c;
    }
    result[ri] = '\0';
    return result;
}
