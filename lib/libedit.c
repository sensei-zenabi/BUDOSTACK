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
#include <strings.h>
#include <ctype.h>

/*
 * Syntax highlight colors (RGB definitions)
 * Update the RGB values below to customize highlight colors.
 */
static const unsigned char COLOR_PREPROCESSOR_RGB[3] = {204, 102, 255}; // Preprocessor directives
static const unsigned char COLOR_COMMENT_RGB[3]      = {144, 144, 144}; // Comments
static const unsigned char COLOR_STRING_RGB[3]       = {0, 204, 102};   // Strings and character literals
static const unsigned char COLOR_KEYWORD_RGB[3]      = {0, 102, 255};   // Control flow keywords
static const unsigned char COLOR_KEYTYPE_RGB[3]      = {0, 204, 204};   // Data type keywords
static const unsigned char COLOR_FUNCTION_RGB[3]     = {102, 255, 255}; // Function names
static const unsigned char COLOR_NUMBER_RGB[3]       = {255, 204, 0};   // Numeric literals
static const unsigned char COLOR_PAREN_RGB[3]        = {255, 0, 255};   // Parentheses and brackets
static const unsigned char COLOR_HEADER_RGB[3]       = {255, 85, 85};   // Markdown headers
static const unsigned char COLOR_BULLET_RGB[3]       = {0, 204, 102};   // List bullets
static const unsigned char COLOR_TAG_RGB[3]          = {85, 170, 255};  // Markup tags
static const unsigned char COLOR_CODE_RGB[3]         = {85, 255, 255};  // Inline code spans
static const unsigned char COLOR_BOLD_RGB[3]         = {255, 255, 102}; // Bold text
static const unsigned char COLOR_ITALIC_RGB[3]       = {255, 102, 204}; // Italic text

static size_t append_color(char *dest, size_t pos, size_t size,
                           const unsigned char rgb[3]) {
    char seq[32];
    int written = snprintf(seq, sizeof(seq), "\x1b[38;2;%u;%u;%um", rgb[0], rgb[1], rgb[2]);
    if (written <= 0)
        return pos;
    size_t w = (size_t)written;
    if (pos + w >= size)
        return pos;
    memcpy(dest + pos, seq, w);
    return pos + w;
}

static size_t append_reset(char *dest, size_t pos, size_t size) {
    static const char reset[] = "\x1b[0m";
    size_t rl = sizeof(reset) - 1;
    if (pos + rl >= size)
        return pos;
    memcpy(dest + pos, reset, rl);
    return pos + rl;
}

int libedit_is_plain_text(const char *filename) {
    if (!filename)
        return 0;

    const char *ext = strrchr(filename, '.');
    if (!ext)
        return 0;

    return strcasecmp(ext, ".txt") == 0;
}

char *highlight_c_line(const char *line, int hl_in_comment) {
    size_t len = strlen(line);
    /* Allocate a generous buffer to hold extra escape codes. */
    size_t buf_size = len * 25 + 32;
    char *result = malloc(buf_size);
    if (!result)
        return NULL;
    size_t ri = 0;  // result buffer index

    /* --- Check for preprocessor directive (macros, defines, etc.) --- */
    size_t j = 0;
    while (j < len && isspace((unsigned char)line[j]))
        j++;
    if (j < len && line[j] == '#') {
        ri = append_color(result, ri, buf_size, COLOR_PREPROCESSOR_RGB);
        /* Copy the entire preprocessor directive line */
        memcpy(result + ri, line, len);
        ri += len;
        ri = append_reset(result, ri, buf_size);
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
                ri = append_color(result, ri, buf_size, COLOR_COMMENT_RGB);
            }
            /* Check for end of multi-line comment */
            if (i + 1 < len && line[i] == '*' && line[i + 1] == '/') {
                result[ri++] = '*';
                result[ri++] = '/';
                ri = append_reset(result, ri, buf_size);
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
                ri = append_color(result, ri, buf_size, COLOR_COMMENT_RGB);
                result[ri++] = '/';
                result[ri++] = '*';
                i += 2;
                in_comment = 1;
                continue;
            }
            /* Check for single-line comment ("//") */
            if (i + 1 < len && line[i] == '/' && line[i + 1] == '/') {
                ri = append_color(result, ri, buf_size, COLOR_COMMENT_RGB);
                while (i < len) {
                    result[ri++] = line[i++];
                }
                ri = append_reset(result, ri, buf_size);
                break;
            }
            /* Check for string literal start */
            if (line[i] == '"') {
                ri = append_color(result, ri, buf_size, COLOR_STRING_RGB);
                result[ri++] = line[i++];  // copy starting quote
                while (i < len) {
                    result[ri++] = line[i];
                    if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
                        i++;
                        break;
                    }
                    i++;
                }
                ri = append_reset(result, ri, buf_size);
                continue;
            }
            /* Check for character literal start */
            if (line[i] == '\'') {
                ri = append_color(result, ri, buf_size, COLOR_STRING_RGB);
                result[ri++] = line[i++];  // copy starting single quote
                while (i < len) {
                    result[ri++] = line[i];
                    if (line[i] == '\'' && (i == 0 || line[i - 1] != '\\')) {
                        i++;
                        break;
                    }
                    i++;
                }
                ri = append_reset(result, ri, buf_size);
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
                    const unsigned char *keyword_rgb =
                        is_data_type ? COLOR_KEYTYPE_RGB : COLOR_KEYWORD_RGB;
                    ri = append_color(result, ri, buf_size, keyword_rgb);
                    for (size_t k = i; k < j; k++) {
                        result[ri++] = line[k];
                    }
                    ri = append_reset(result, ri, buf_size);
                    i = j;
                    continue;
                } else {
                    size_t k2 = j;
                    while (k2 < len && isspace((unsigned char)line[k2]))
                        k2++;
                    if (k2 < len && line[k2] == '(') {
                        ri = append_color(result, ri, buf_size, COLOR_FUNCTION_RGB);
                        for (size_t k = i; k < j; k++) {
                            result[ri++] = line[k];
                        }
                        ri = append_reset(result, ri, buf_size);
                        i = j;
                        continue;
                    }
                }
            }
            /* Check for numeric literals */
            if (isdigit((unsigned char)line[i])) {
                ri = append_color(result, ri, buf_size, COLOR_NUMBER_RGB);
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
                ri = append_reset(result, ri, buf_size);
                continue;
            }
            /* Check for parentheses and brackets */
            if (strchr("(){}[]", line[i])) {
                ri = append_color(result, ri, buf_size, COLOR_PAREN_RGB);
                result[ri++] = line[i++];
                ri = append_reset(result, ri, buf_size);
                continue;
            }
            /* Default: copy the character unchanged */
            result[ri++] = line[i++];
        }
    }
    /* If still inside a multi-line comment, append a reset to ensure color does not leak */
    if (in_comment) {
        ri = append_reset(result, ri, buf_size);
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
    size_t buf_size = len * 25 + 32;  // Allocate extra space for escape codes
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
        ri = append_color(result, ri, buf_size, COLOR_HEADER_RGB);
        // Copy the rest of the line as header text
        while (i < len) {
            result[ri++] = line[i++];
        }
        ri = append_reset(result, ri, buf_size);
    } else {
        // Highlight list bullets (-, * or +) followed by a space
        if (i < len && (line[i] == '-' || line[i] == '*' || line[i] == '+') &&
            (i + 1 < len && line[i + 1] == ' ')) {
            ri = append_color(result, ri, buf_size, COLOR_BULLET_RGB);
            result[ri++] = line[i++];
            result[ri++] = line[i++];
            ri = append_reset(result, ri, buf_size);
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
                ri = append_color(result, ri, buf_size, COLOR_CODE_RGB);
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
                ri = append_reset(result, ri, buf_size);
                i--; // adjust for the outer loop increment
                continue;
            }
            // Check for bold and italic markers if not inside a tag
            if (!in_tag && (c == '*' || c == '_')) {
                char marker = c;
                if (i + 1 < len && line[i + 1] == marker) {
                    int has_closing = 0;
                    for (size_t k = i + 2; k + 1 < len; k++) {
                        if (line[k] == marker && line[k + 1] == marker) {
                            has_closing = 1;
                            break;
                        }
                    }
                    if (!in_bold && has_closing) {
                        ri = append_color(result, ri, buf_size, COLOR_BOLD_RGB);
                        result[ri++] = marker;
                        result[ri++] = marker;
                        in_bold = 1;
                        bold_marker = marker;
                        i++;
                        continue;
                    } else if (bold_marker == marker) {
                        result[ri++] = marker;
                        result[ri++] = marker;
                        ri = append_reset(result, ri, buf_size);
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
                    int has_closing = 0;
                    for (size_t k = i + 1; k < len; k++) {
                        if (line[k] == marker) {
                            has_closing = 1;
                            break;
                        }
                    }
                    if (!in_italic && has_closing) {
                        ri = append_color(result, ri, buf_size, COLOR_ITALIC_RGB);
                        result[ri++] = marker;
                        in_italic = 1;
                        italic_marker = marker;
                        continue;
                    } else if (italic_marker == marker) {
                        result[ri++] = marker;
                        ri = append_reset(result, ri, buf_size);
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
                /* Only treat the '<' as a markup tag if a closing '>' exists later */
                if (strchr(line + i + 1, '>')) {
                    ri = append_color(result, ri, buf_size, COLOR_TAG_RGB);
                    in_tag = 1;
                }
            }
            // Copy the current character
            result[ri++] = c;
            // Check for end of a markup tag
            if (c == '>' && in_tag) {
                ri = append_reset(result, ri, buf_size);
                in_tag = 0;
            }
        }
        // If any formatting is still active, reset it at the end
        if (in_tag || in_bold || in_italic) {
            ri = append_reset(result, ri, buf_size);
        }
    }
    result[ri] = '\0';
    return result;
}
