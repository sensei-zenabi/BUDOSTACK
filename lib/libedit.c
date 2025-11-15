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

#include "retroprofile.h"

#define ANSI_RESET "\x1b[0m"

static size_t format_color_sequence(char *buffer,
                                    size_t size,
                                    RetroFormatRole role,
                                    const char *style_prefix) {
    if (buffer == NULL || size == 0)
        return 0;

    RetroColor color;
    if (retroprofile_active_format_color(role, &color) != 0) {
        buffer[0] = '\0';
        return 0;
    }

    if (style_prefix == NULL)
        style_prefix = "";

    int written;
    if (style_prefix[0] != '\0') {
        written = snprintf(buffer,
                           size,
                           "\x1b[%s38;2;%u;%u;%um",
                           style_prefix,
                           color.r,
                           color.g,
                           color.b);
    } else {
        written = snprintf(buffer,
                           size,
                           "\x1b[38;2;%u;%u;%um",
                           color.r,
                           color.g,
                           color.b);
    }

    if (written < 0) {
        buffer[0] = '\0';
        return 0;
    }

    if ((size_t)written >= size) {
        buffer[size - 1] = '\0';
        return size - 1;
    }

    return (size_t)written;
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
    size_t buf_size = len * 5 + 1;
    char *result = malloc(buf_size);
    if (!result)
        return NULL;
    size_t ri = 0;  // result buffer index

    char preprocessor_color[32];
    char comment_color[32];
    char string_color[32];
    char character_color[32];
    char keyword_color[32];
    char keyword_type_color[32];
    char function_color[32];
    char number_color[32];
    char punctuation_color[32];

    size_t preprocessor_len = format_color_sequence(preprocessor_color,
                                                    sizeof(preprocessor_color),
                                                    RETROPROFILE_FORMAT_C_PREPROCESSOR,
                                                    "");
    size_t comment_len = format_color_sequence(comment_color,
                                               sizeof(comment_color),
                                               RETROPROFILE_FORMAT_C_COMMENT,
                                               "");
    size_t string_len = format_color_sequence(string_color,
                                              sizeof(string_color),
                                              RETROPROFILE_FORMAT_C_STRING,
                                              "");
    size_t character_len = format_color_sequence(character_color,
                                                 sizeof(character_color),
                                                 RETROPROFILE_FORMAT_C_CHARACTER,
                                                 "");
    size_t keyword_len = format_color_sequence(keyword_color,
                                               sizeof(keyword_color),
                                               RETROPROFILE_FORMAT_C_KEYWORD,
                                               "");
    size_t keyword_type_len = format_color_sequence(keyword_type_color,
                                                    sizeof(keyword_type_color),
                                                    RETROPROFILE_FORMAT_C_KEYWORD_TYPE,
                                                    "");
    size_t function_len = format_color_sequence(function_color,
                                                sizeof(function_color),
                                                RETROPROFILE_FORMAT_C_FUNCTION,
                                                "");
    size_t number_len = format_color_sequence(number_color,
                                              sizeof(number_color),
                                              RETROPROFILE_FORMAT_C_NUMBER,
                                              "");
    size_t punctuation_len = format_color_sequence(punctuation_color,
                                                   sizeof(punctuation_color),
                                                   RETROPROFILE_FORMAT_C_PUNCTUATION,
                                                   "");
    const size_t reset_len = strlen(ANSI_RESET);

    /* --- Check for preprocessor directive (macros, defines, etc.) --- */
    size_t j = 0;
    while (j < len && isspace((unsigned char)line[j]))
        j++;
    if (j < len && line[j] == '#') {
        if (preprocessor_len > 0 && ri + preprocessor_len < buf_size) {
            memcpy(result + ri, preprocessor_color, preprocessor_len);
            ri += preprocessor_len;
        }
        /* Copy the entire preprocessor directive line */
        memcpy(result + ri, line, len);
        ri += len;
        if (reset_len > 0 && ri + reset_len < buf_size) {
            memcpy(result + ri, ANSI_RESET, reset_len);
            ri += reset_len;
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
            if (i == 0 && comment_len > 0) {
                memcpy(result + ri, comment_color, comment_len);
                ri += comment_len;
            }
            /* Check for end of multi-line comment */
            if (i + 1 < len && line[i] == '*' && line[i + 1] == '/') {
                result[ri++] = '*';
                result[ri++] = '/';
                if (reset_len > 0) {
                    memcpy(result + ri, ANSI_RESET, reset_len);
                    ri += reset_len;
                }
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
                if (comment_len > 0) {
                    memcpy(result + ri, comment_color, comment_len);
                    ri += comment_len;
                }
                result[ri++] = '/';
                result[ri++] = '*';
                i += 2;
                in_comment = 1;
                continue;
            }
            /* Check for single-line comment ("//") */
            if (i + 1 < len && line[i] == '/' && line[i + 1] == '/') {
                if (comment_len > 0) {
                    memcpy(result + ri, comment_color, comment_len);
                    ri += comment_len;
                }
                while (i < len) {
                    result[ri++] = line[i++];
                }
                if (reset_len > 0) {
                    memcpy(result + ri, ANSI_RESET, reset_len);
                    ri += reset_len;
                }
                break;
            }
            /* Check for string literal start */
            if (line[i] == '"') {
                if (string_len > 0) {
                    memcpy(result + ri, string_color, string_len);
                    ri += string_len;
                }
                result[ri++] = line[i++];  // copy starting quote
                while (i < len) {
                    result[ri++] = line[i];
                    if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
                        i++;
                        break;
                    }
                    i++;
                }
                if (reset_len > 0) {
                    memcpy(result + ri, ANSI_RESET, reset_len);
                    ri += reset_len;
                }
                continue;
            }
            /* Check for character literal start */
            if (line[i] == '\'') {
                size_t active_char_len = character_len > 0 ? character_len : string_len;
                const char *active_char_color = character_len > 0 ? character_color : string_color;
                if (active_char_len > 0) {
                    memcpy(result + ri, active_char_color, active_char_len);
                    ri += active_char_len;
                }
                result[ri++] = line[i++];  // copy starting single quote
                while (i < len) {
                    result[ri++] = line[i];
                    if (line[i] == '\'' && (i == 0 || line[i - 1] != '\\')) {
                        i++;
                        break;
                    }
                    i++;
                }
                if (reset_len > 0) {
                    memcpy(result + ri, ANSI_RESET, reset_len);
                    ri += reset_len;
                }
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
                    const char *color = is_data_type ? keyword_type_color : keyword_color;
                    size_t cl = is_data_type ? keyword_type_len : keyword_len;
                    if (cl > 0) {
                        memcpy(result + ri, color, cl);
                        ri += cl;
                    }
                    for (size_t k = i; k < j; k++) {
                        result[ri++] = line[k];
                    }
                    if (reset_len > 0) {
                        memcpy(result + ri, ANSI_RESET, reset_len);
                        ri += reset_len;
                    }
                    i = j;
                    continue;
                } else {
                    size_t k2 = j;
                    while (k2 < len && isspace((unsigned char)line[k2]))
                        k2++;
                    if (k2 < len && line[k2] == '(') {
                        if (function_len > 0) {
                            memcpy(result + ri, function_color, function_len);
                            ri += function_len;
                        }
                        for (size_t k = i; k < j; k++) {
                            result[ri++] = line[k];
                        }
                        if (reset_len > 0) {
                            memcpy(result + ri, ANSI_RESET, reset_len);
                            ri += reset_len;
                        }
                        i = j;
                        continue;
                    }
                }
            }
            /* Check for numeric literals */
            if (isdigit((unsigned char)line[i])) {
                if (number_len > 0) {
                    memcpy(result + ri, number_color, number_len);
                    ri += number_len;
                }
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
                if (reset_len > 0) {
                    memcpy(result + ri, ANSI_RESET, reset_len);
                    ri += reset_len;
                }
                continue;
            }
            /* Check for parentheses and brackets */
            if (strchr("(){}[]", line[i])) {
                if (punctuation_len > 0) {
                    memcpy(result + ri, punctuation_color, punctuation_len);
                    ri += punctuation_len;
                }
                result[ri++] = line[i++];
                if (reset_len > 0) {
                    memcpy(result + ri, ANSI_RESET, reset_len);
                    ri += reset_len;
                }
                continue;
            }
            /* Default: copy the character unchanged */
            result[ri++] = line[i++];
        }
    }
    /* If still inside a multi-line comment, append a reset to ensure color does not leak */
    if (in_comment && reset_len > 0) {
        if (ri + reset_len < buf_size) {
            memcpy(result + ri, ANSI_RESET, reset_len);
            ri += reset_len;
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

    char header_color_seq[32];
    char bullet_color_seq[32];
    char code_color_seq[32];
    char bold_color_seq[32];
    char italic_color_seq[32];
    char tag_color_seq[32];

    size_t header_color_len = format_color_sequence(header_color_seq,
                                                    sizeof(header_color_seq),
                                                    RETROPROFILE_FORMAT_TEXT_HEADER,
                                                    "");
    size_t bullet_color_len = format_color_sequence(bullet_color_seq,
                                                    sizeof(bullet_color_seq),
                                                    RETROPROFILE_FORMAT_TEXT_BULLET,
                                                    "");
    size_t code_color_len = format_color_sequence(code_color_seq,
                                                  sizeof(code_color_seq),
                                                  RETROPROFILE_FORMAT_TEXT_CODE,
                                                  "");
    size_t bold_color_len = format_color_sequence(bold_color_seq,
                                                  sizeof(bold_color_seq),
                                                  RETROPROFILE_FORMAT_TEXT_BOLD,
                                                  "1;");
    size_t italic_color_len = format_color_sequence(italic_color_seq,
                                                    sizeof(italic_color_seq),
                                                    RETROPROFILE_FORMAT_TEXT_ITALIC,
                                                    "3;");
    size_t tag_color_len = format_color_sequence(tag_color_seq,
                                                 sizeof(tag_color_seq),
                                                 RETROPROFILE_FORMAT_TEXT_TAG,
                                                 "");
    const size_t reset_len = strlen(ANSI_RESET);

    size_t i = 0;
    // Copy any leading whitespace
    while (i < len && isspace((unsigned char)line[i])) {
        result[ri++] = line[i++];
    }
    // Check for Markdown header (line starting with '#')
    if (i < len && line[i] == '#') {
        if (header_color_len > 0 && ri + header_color_len < buf_size) {
            memcpy(result + ri, header_color_seq, header_color_len);
            ri += header_color_len;
        }
        // Copy the rest of the line as header text
        while (i < len) {
            result[ri++] = line[i++];
        }
        if (reset_len > 0 && ri + reset_len < buf_size) {
            memcpy(result + ri, ANSI_RESET, reset_len);
            ri += reset_len;
        }
    } else {
        // Highlight list bullets (-, * or +) followed by a space
        if (i < len && (line[i] == '-' || line[i] == '*' || line[i] == '+') &&
            (i + 1 < len && line[i + 1] == ' ')) {
            if (bullet_color_len > 0 && ri + bullet_color_len < buf_size) {
                memcpy(result + ri, bullet_color_seq, bullet_color_len);
                ri += bullet_color_len;
            }
            result[ri++] = line[i++];
            result[ri++] = line[i++];
            if (reset_len > 0 && ri + reset_len < buf_size) {
                memcpy(result + ri, ANSI_RESET, reset_len);
                ri += reset_len;
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
                if (code_color_len > 0 && ri + code_color_len < buf_size) {
                    memcpy(result + ri, code_color_seq, code_color_len);
                    ri += code_color_len;
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
                if (reset_len > 0 && ri + reset_len < buf_size) {
                    memcpy(result + ri, ANSI_RESET, reset_len);
                    ri += reset_len;
                }
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
                        if (bold_color_len > 0 && ri + bold_color_len < buf_size) {
                            memcpy(result + ri, bold_color_seq, bold_color_len);
                            ri += bold_color_len;
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
                        if (reset_len > 0 && ri + reset_len < buf_size) {
                            memcpy(result + ri, ANSI_RESET, reset_len);
                            ri += reset_len;
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
                    int has_closing = 0;
                    for (size_t k = i + 1; k < len; k++) {
                        if (line[k] == marker) {
                            has_closing = 1;
                            break;
                        }
                    }
                    if (!in_italic && has_closing) {
                        if (italic_color_len > 0 && ri + italic_color_len < buf_size) {
                            memcpy(result + ri, italic_color_seq, italic_color_len);
                            ri += italic_color_len;
                        }
                        result[ri++] = marker;
                        in_italic = 1;
                        italic_marker = marker;
                        continue;
                    } else if (italic_marker == marker) {
                        result[ri++] = marker;
                        if (reset_len > 0 && ri + reset_len < buf_size) {
                            memcpy(result + ri, ANSI_RESET, reset_len);
                            ri += reset_len;
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
                /* Only treat the '<' as a markup tag if a closing '>' exists later */
                if (strchr(line + i + 1, '>')) {
                    if (tag_color_len > 0 && ri + tag_color_len < buf_size) {
                        memcpy(result + ri, tag_color_seq, tag_color_len);
                        ri += tag_color_len;
                    }
                    in_tag = 1;
                }
            }
            // Copy the current character
            result[ri++] = c;
            // Check for end of a markup tag
            if (c == '>' && in_tag) {
                if (reset_len > 0 && ri + reset_len < buf_size) {
                    memcpy(result + ri, ANSI_RESET, reset_len);
                    ri += reset_len;
                }
                in_tag = 0;
            }
        }
        // If any formatting is still active, reset it at the end
        if ((in_tag || in_bold || in_italic) && reset_len > 0) {
            if (ri + reset_len < buf_size) {
                memcpy(result + ri, ANSI_RESET, reset_len);
                ri += reset_len;
            }
        }
    }
    result[ri] = '\0';
    return result;
}
