/*
 * highlight_c_line (extended industry standard C highlighting)
 *
 * This version extends the syntax highlighter to provide industry standard
 * highlighting for C source code. It highlights:
 *   - C keywords: control keywords in blue and data type keywords in cyan.
 *   - Numeric literals in yellow (supports decimal, hex, binary and exponents).
 *   - String and character literals in green.
 *   - Single-line comments (dashdash) and multi-line comments (dashstar ... stardash) in gray.
 *   - Parentheses, braces and brackets in magenta.
 *   - Function names in bright cyan.
 *
 * Design principles:
 *  - Plain C using -std=c11 and only standard libraries.
 *  - No separate header files.
 *  - Inline comments document the design choices.
 *
 * The caller is responsible for freeing the returned string.
 *
 * Note: The function now accepts an extra parameter (hl_in_comment) which indicates
 * whether the line begins inside a multi-line comment.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *highlight_c_line(const char *line, int hl_in_comment) {
    size_t len = strlen(line);
    /* Allocate a generous buffer to hold extra escape codes. */
    size_t buf_size = len * 5 + 1;
    char *result = malloc(buf_size);
    if (!result)
        return NULL;
    size_t ri = 0;  // result buffer index

    /* --- Check for preprocessor directive (macros, defines, etc.) --- */
    size_t j = 0;
    while (j < len && isspace((unsigned char)line[j]))
        j++;
    if (j < len && line[j] == '#') {
        const char *prep_color = "\x1b[95m";  // bright magenta for preprocessor directives
        size_t cl = strlen(prep_color);
        if (ri + cl < buf_size) {
            memcpy(result + ri, prep_color, cl);
            ri += cl;
        }
        /* Copy the entire preprocessor directive line */
        memcpy(result + ri, line, len);
        ri += len;
        const char *reset = "\x1b[0m";
        size_t rl = strlen(reset);
        if (ri + rl < buf_size) {
            memcpy(result + ri, reset, rl);
            ri += rl;
        }
        result[ri] = '\0';
        return result;
    }

    /* --- Begin processing the line with multi-line comment support --- */
    int in_comment = hl_in_comment;
    size_t i = 0;
    while (i < len) {
        if (in_comment) {
            /* If we are inside a multi-line comment, ensure the comment color is active. */
            if (i == 0) {
                const char *comment_color = "\x1b[90m";  // gray
                size_t cl = strlen(comment_color);
                memcpy(result + ri, comment_color, cl);
                ri += cl;
            }
            /* Check for end of multi-line comment */
            if (i + 1 < len && line[i] == '*' && line[i + 1] == '/') {
                result[ri++] = '*';
                result[ri++] = '/';
                const char *reset = "\x1b[0m";
                size_t rl = strlen(reset);
                memcpy(result + ri, reset, rl);
                ri += rl;
                i += 2;
                in_comment = 0;
                continue;
            } else {
                result[ri++] = line[i++];
                continue;
            }
        } else {
            /* Check for start of multi-line comment */
            if (i + 1 < len && line[i] == '/' && line[i + 1] == '*') {
                const char *comment_color = "\x1b[90m";  // gray
                size_t cl = strlen(comment_color);
                memcpy(result + ri, comment_color, cl);
                ri += cl;
                result[ri++] = '/';
                result[ri++] = '*';
                i += 2;
                in_comment = 1;
                continue;
            }
            /* Check for single-line comment ("//") */
            if (i + 1 < len && line[i] == '/' && line[i + 1] == '/') {
                const char *comment_color = "\x1b[90m";  // gray
                size_t cl = strlen(comment_color);
                memcpy(result + ri, comment_color, cl);
                ri += cl;
                while (i < len) {
                    result[ri++] = line[i++];
                }
                const char *reset = "\x1b[0m";
                size_t rl = strlen(reset);
                memcpy(result + ri, reset, rl);
                ri += rl;
                break;
            }
            /* Check for string literal start */
            if (line[i] == '"') {
                const char *str_color = "\x1b[32m";  // green
                size_t cl = strlen(str_color);
                memcpy(result + ri, str_color, cl);
                ri += cl;
                result[ri++] = line[i++];  // copy starting quote
                while (i < len) {
                    result[ri++] = line[i];
                    if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
                        i++;
                        break;
                    }
                    i++;
                }
                const char *reset = "\x1b[0m";
                size_t rl = strlen(reset);
                memcpy(result + ri, reset, rl);
                ri += rl;
                continue;
            }
            /* Check for character literal start */
            if (line[i] == '\'') {
                const char *char_color = "\x1b[32m";  // green
                size_t cl = strlen(char_color);
                memcpy(result + ri, char_color, cl);
                ri += cl;
                result[ri++] = line[i++];  // copy starting single quote
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
                memcpy(result + ri, reset, rl);
                ri += rl;
                continue;
            }
            /* Check for identifier (potential keyword) */
            if ((line[i] == '_' || isalpha((unsigned char)line[i])) &&
                (i == 0 || (!isalnum((unsigned char)line[i - 1]) && line[i - 1] != '_'))) {
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
                    memcpy(result + ri, color, cl);
                    ri += cl;
                    for (size_t k = i; k < j; k++) {
                        result[ri++] = line[k];
                    }
                    const char *reset = "\x1b[0m";
                    size_t rl = strlen(reset);
                    memcpy(result + ri, reset, rl);
                    ri += rl;
                    i = j;
                    continue;
                } else {
                    size_t k2 = j;
                    while (k2 < len && isspace((unsigned char)line[k2]))
                        k2++;
                    if (k2 < len && line[k2] == '(') {
                        const char *fn_color = "\x1b[96m";  // bright cyan for function names
                        size_t cl2 = strlen(fn_color);
                        memcpy(result + ri, fn_color, cl2);
                        ri += cl2;
                        for (size_t k = i; k < j; k++) {
                            result[ri++] = line[k];
                        }
                        const char *reset = "\x1b[0m";
                        size_t rl2 = strlen(reset);
                        memcpy(result + ri, reset, rl2);
                        ri += rl2;
                        i = j;
                        continue;
                    }
                }
            }
            /* Check for numeric literals */
            if (isdigit((unsigned char)line[i])) {
                const char *num_color = "\x1b[33m";  // yellow
                size_t cl = strlen(num_color);
                memcpy(result + ri, num_color, cl);
                ri += cl;
                if (line[i] == '0' && i + 1 < len) {
                    if (line[i + 1] == 'x' || line[i + 1] == 'X') {
                        result[ri++] = line[i++];
                        result[ri++] = line[i++];
                        while (i < len && isxdigit((unsigned char)line[i])) {
                            result[ri++] = line[i++];
                        }
                    } else if (line[i + 1] == 'b' || line[i + 1] == 'B') {
                        result[ri++] = line[i++];
                        result[ri++] = line[i++];
                        while (i < len && (line[i] == '0' || line[i] == '1')) {
                            result[ri++] = line[i++];
                        }
                    } else {
                        while (i < len && isdigit((unsigned char)line[i])) {
                            result[ri++] = line[i++];
                        }
                    }
                } else {
                    while (i < len && isdigit((unsigned char)line[i])) {
                        result[ri++] = line[i++];
                    }
                }
                if (i < len && line[i] == '.') {
                    result[ri++] = line[i++];
                    while (i < len && isdigit((unsigned char)line[i])) {
                        result[ri++] = line[i++];
                    }
                }
                if (i < len && (line[i] == 'e' || line[i] == 'E')) {
                    result[ri++] = line[i++];
                    if (i < len && (line[i] == '+' || line[i] == '-')) {
                        result[ri++] = line[i++];
                    }
                    while (i < len && isdigit((unsigned char)line[i])) {
                        result[ri++] = line[i++];
                    }
                }
                const char *reset = "\x1b[0m";
                size_t rl = strlen(reset);
                memcpy(result + ri, reset, rl);
                ri += rl;
                continue;
            }
            /* Check for parentheses and brackets */
            if (strchr("(){}[]", line[i])) {
                const char *paren_color = "\x1b[35m";  // magenta
                size_t cl = strlen(paren_color);
                memcpy(result + ri, paren_color, cl);
                ri += cl;
                result[ri++] = line[i++];
                const char *reset = "\x1b[0m";
                size_t rl = strlen(reset);
                memcpy(result + ri, reset, rl);
                ri += rl;
                continue;
            }
            /* Default: copy the character unchanged */
            result[ri++] = line[i++];
        }
    }
    /* If still inside a multi-line comment, append a reset to ensure color does not leak */
    if (in_comment) {
        const char *reset = "\x1b[0m";
        size_t rl = strlen(reset);
        if (ri + rl < buf_size) {
            memcpy(result + ri, reset, rl);
            ri += rl;
        }
    }
    result[ri] = '\0';
    return result;
}

/*
 * highlight_other_line (general text and markup language highlighting)
 *
 * This updated version provides syntax highlighting for:
 *   - Markdown headers: if the first non-space character is '#' the entire line is colored red.
 *   - List bullets (-, * or + followed by space) in green.
 *   - Markup tags: any content between '<' and '>' is colored blue.
 *   - Inline code spans: text enclosed in backticks (`) is colored cyan.
 *   - Bold text: text enclosed in double asterisks (**) or underscores (__) is colored bold yellow.
 *   - Italic text: text enclosed in single asterisks (*) or underscores (_) is colored italic magenta.
 *
 * Design principles:
 *  - Plain C using -std=c11 and only standard libraries.
 *  - No separate header files.
 *  - The function processes the input line character by character and applies ANSI
 *    escape sequences for coloring.
 *
 * The caller is responsible for freeing the returned string.
 */
char *highlight_other_line(const char *line) {
    size_t len = strlen(line);
    size_t buf_size = len * 5 + 1;  // Allocate extra space for escape codes
    char *result = malloc(buf_size);
    if (!result)
        return NULL;
    size_t ri = 0;  // result index

    size_t i = 0;
    // Copy any leading whitespace
    while (i < len && isspace((unsigned char)line[i])) {
        result[ri++] = line[i++];
    }
    // Check for Markdown header (line starting with '#')
    if (i < len && line[i] == '#') {
        const char *header_color = "\x1b[31m";  // red for headers
        size_t cl = strlen(header_color);
        if (ri + cl < buf_size) {
            memcpy(result + ri, header_color, cl);
            ri += cl;
        }
        // Copy the rest of the line as header text
        while (i < len) {
            result[ri++] = line[i++];
        }
        const char *reset = "\x1b[0m";
        size_t rl = strlen(reset);
        if (ri + rl < buf_size) {
            memcpy(result + ri, reset, rl);
            ri += rl;
        }
    } else {
        // Highlight list bullets (-, * or +) followed by a space
        if (i < len && (line[i] == '-' || line[i] == '*' || line[i] == '+') &&
            (i + 1 < len && line[i + 1] == ' ')) {
            const char *bullet_color = "\x1b[32m";  // green for bullets
            size_t cl = strlen(bullet_color);
            if (ri + cl < buf_size) {
                memcpy(result + ri, bullet_color, cl);
                ri += cl;
            }
            result[ri++] = line[i++];
            result[ri++] = line[i++];
            const char *reset = "\x1b[0m";
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
        }
        // Process the line for markup tags, inline code, bold and italic markers
        int in_tag = 0;     // inside a <...> tag
        int in_bold = 0;    // inside a bold span
        int in_italic = 0;  // inside an italic span
        char bold_marker = 0;
        char italic_marker = 0;
        for (; i < len; i++) {
            char c = line[i];
            // Check for inline code span start (backtick) if not inside a tag
            if (!in_tag && c == '`') {
                const char *code_color = "\x1b[36m";  // cyan for inline code
                size_t cl = strlen(code_color);
                if (ri + cl < buf_size) {
                    memcpy(result + ri, code_color, cl);
                    ri += cl;
                }
                // Append the starting backtick
                result[ri++] = c;
                i++;  // move past the starting backtick
                // Copy characters until a closing backtick is found or end of line
                while (i < len && line[i] != '`') {
                    result[ri++] = line[i++];
                }
                // If closing backtick found, copy it as well
                if (i < len && line[i] == '`') {
                    result[ri++] = line[i++];
                }
                const char *reset = "\x1b[0m";
                size_t rl = strlen(reset);
                if (ri + rl < buf_size) {
                    memcpy(result + ri, reset, rl);
                    ri += rl;
                }
                i--; // adjust for the outer loop increment
                continue;
            }
            // Check for bold and italic markers if not inside a tag
            if (!in_tag && (c == '*' || c == '_')) {
                char marker = c;
                if (i + 1 < len && line[i + 1] == marker) {
                    if (!in_bold) {
                        const char *bold_color = "\x1b[1;33m";  // bold yellow
                        size_t cl = strlen(bold_color);
                        if (ri + cl < buf_size) {
                            memcpy(result + ri, bold_color, cl);
                            ri += cl;
                        }
                        result[ri++] = marker;
                        result[ri++] = marker;
                        in_bold = 1;
                        bold_marker = marker;
                        i++;
                        continue;
                    } else if (bold_marker == marker) {
                        result[ri++] = marker;
                        result[ri++] = marker;
                        const char *reset = "\x1b[0m";
                        size_t rl = strlen(reset);
                        if (ri + rl < buf_size) {
                            memcpy(result + ri, reset, rl);
                            ri += rl;
                        }
                        in_bold = 0;
                        bold_marker = 0;
                        i++;
                        continue;
                    } else {
                        result[ri++] = marker;
                        continue;
                    }
                } else {
                    if (in_bold && bold_marker == marker) {
                        result[ri++] = marker;
                        continue;
                    }
                    if (!in_italic) {
                        const char *italic_color = "\x1b[3;35m";  // italic magenta
                        size_t cl = strlen(italic_color);
                        if (ri + cl < buf_size) {
                            memcpy(result + ri, italic_color, cl);
                            ri += cl;
                        }
                        result[ri++] = marker;
                        in_italic = 1;
                        italic_marker = marker;
                        continue;
                    } else if (italic_marker == marker) {
                        result[ri++] = marker;
                        const char *reset = "\x1b[0m";
                        size_t rl = strlen(reset);
                        if (ri + rl < buf_size) {
                            memcpy(result + ri, reset, rl);
                            ri += rl;
                        }
                        in_italic = 0;
                        italic_marker = 0;
                        continue;
                    } else {
                        result[ri++] = marker;
                        continue;
                    }
                }
            }
            // Check for start of a markup tag
            if (c == '<') {
                const char *tag_color = "\x1b[34m";  // blue for tags
                size_t cl = strlen(tag_color);
                if (ri + cl < buf_size) {
                    memcpy(result + ri, tag_color, cl);
                    ri += cl;
                }
                in_tag = 1;
            }
            // Copy the current character
            result[ri++] = c;
            // Check for end of a markup tag
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
        // If any formatting is still active, reset it at the end
        if (in_bold || in_italic) {
            const char *reset = "\x1b[0m";
            size_t rl = strlen(reset);
            if (ri + rl < buf_size) {
                memcpy(result + ri, reset, rl);
                ri += rl;
            }
        }
    }
    result[ri] = '\0';
    return result;
}
